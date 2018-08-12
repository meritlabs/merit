// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog2/cgs.h"
#include "addressindex.h"
#include "validation.h"
#include "referrals.h"
#include "ctpl/ctpl.h"
#include "sync.h"

#include <stack>
#include <deque>
#include <stack>
#include <numeric>

#include <boost/multiprecision/cpp_int.hpp> 
#include <boost/thread/thread.hpp>

namespace pog2
{
    namespace
    {
        const size_t BATCH_SIZE = 100;
        const int NO_GENESIS = 13500;
        ctpl::thread_pool cgs_pool;
    }

    void SetupCgsThreadPool(size_t threads)
    {
        cgs_pool.resize(threads);
    }

    using UnspentPair = std::pair<CAddressUnspentKey, CAddressUnspentValue>;

    using BigInt = boost::multiprecision::cpp_int;

    double Age(int height, int tip_height, double maturity)
    {
        assert(tip_height >= 0);
        assert(height <= tip_height);
        assert(maturity > 0);

        const double maturity_scale = maturity / 4.0; //matures to about 97% at 4
        const double age = (tip_height - height) / maturity_scale;

        assert(age >= 0);
        return age;
    }

    double AgeScale(int height, int tip_height, double maturity)
    {
        assert(tip_height >= 0);
        assert(height <= tip_height);
        assert(maturity > 0);

        const auto age = Age(height, tip_height, maturity);
        const auto age_scale =  1.0 - (1.0 / (std::pow(age, 2) + 1.0));

        assert(age_scale >= 0);
        assert(age_scale <= 1.001);
        return age_scale;
    }

    double AgeScale(const Coin& c, int tip_height, int maturity) {
        assert(tip_height >= 0);
        assert(c.height <= tip_height);
        assert(maturity > 0);
        return AgeScale(c.height, tip_height, maturity);
    }

    int GetReferralHeight(
            referral::ReferralsViewCache& db,
            const referral::Address& a
            )
    {
        auto height = db.GetReferralHeight(a);
        if (height < 0) {
            const auto beacon = db.GetReferral(a);
            assert(beacon);

            uint256 hashBlock;
            CBlockIndex* pindex = nullptr;

            referral::ReferralRef beacon_out;
            if (GetReferral(beacon->GetHash(), beacon_out, hashBlock, pindex)) {
                assert(pindex);
                height = pindex->nHeight;
                if (height > 0) {
                    db.SetReferralHeight(height, a);
                }
            }
        }
        return height;
    }

    Coins GetCoins(
            int height,
            char address_type,
            const referral::Address& address) {
        Coins cs;
        std::vector<UnspentPair> unspent;
        if (!GetAddressUnspent(address, address_type, false, unspent)) {
            return cs;
        }

        cs.reserve(unspent.size());
        for (const auto& p : unspent) {
            if (p.first.type == 0 || p.first.isInvite) {
                continue;
            }
            assert(p.second.satoshis >= 0);

            cs.push_back({std::min(p.second.blockHeight, height), p.second.satoshis});
        }

        return cs;
    }

    bool GetAllCoins(CGSContext& context, int tip_height) {
        std::vector<UnspentPair> unspent;
        if (!GetAllUnspent(false, [&context, tip_height](const CAddressUnspentKey& key, const CAddressUnspentValue& value) {
                if (key.type == 0 || key.isInvite || value.satoshis == 0 || value.blockHeight > tip_height) {
                    return;
                }

                assert(!key.isInvite);
                assert(value.satoshis > 0);

                auto& entrant = context.GetEntrant(key.hashBytes);
                entrant.coins.emplace_back(value.blockHeight, value.satoshis);
           })) {
            return false;
        }
        return true;
    }

    BalancePair BalanceDecay(int tip_height, const Coin& c, int maturity) {
        assert(tip_height >= 0);
        assert(c.height <= tip_height);
        assert(c.amount >= 0);
        assert(maturity > 0);

        const auto age_scale = AgeScale(c, tip_height, maturity);
        const auto aged_balance = age_scale * c.amount; 

        assert(aged_balance <= std::numeric_limits<CAmount>::max());
        
        CAmount amount = aged_balance;

        assert(amount >= 0);
        assert(amount <= c.amount);
        return BalancePair{amount, c.amount};
    }

    template <class AgeFunc>
    BalancePair AgedBalance(int tip_height, const Coins& cs, int maturity, AgeFunc AgedBalanceFunc) {
        assert(tip_height >= 0);

        BalancePairs balances(cs.size());
        std::transform(cs.begin(), cs.end(), balances.begin(),
                [tip_height, maturity, &AgedBalanceFunc](const Coin& c) {
                    return AgedBalanceFunc(tip_height, c, maturity);
                });

        const auto aged_balance = 
            std::accumulate(balances.begin(), balances.end(), CAmount{0}, 
                [](CAmount amount, const BalancePair& b) {
                    return amount + b.first;
               });

        const auto balance =
            std::accumulate(balances.begin(), balances.end(), CAmount{0}, 
                [](CAmount amount, const BalancePair& b) {
                    return amount + b.second;
                });
        
        assert(aged_balance <= balance);
        return BalancePair{aged_balance, balance};
    }

    //Convex function with property that if C0 > C1 and you have A within [0, 1] then 
    //ConvexF(C0 + A) - ConvexF(C0) > ConvexF(C1 + A) - ConvexF(C1);
    //See: Lottery Trees: Motivational Deployment of Networked Systems
    //These properties are important to allow for some kind of growth incentive
    //without compromising the system's integrity against sybil attacks.
    template <class V>
    V ConvexF(V c, V B, V S) { 
        assert(c >= V{0});
        assert(c <= V{1.01});
        assert(B >= V{0});
        assert(B <= V{1.01});
        assert(S >= V{0});
        assert(S <= V{1.01});

        const V v = (B*c) + ((V{1} - B)*boost::multiprecision::pow(c, V{1} + S));
        assert(v >= 0);
        return v;
    }

    Contribution ContributionNode(
            CGSContext& context,
            CachedEntrant& entrant,
            referral::ReferralsViewCache& db)
    {
        assert(context.tip_height > 0);
        assert(context.new_coin_maturity > 0);

        const auto aged_balance = entrant.balances;

        const auto beacon_height = 
            std::min(entrant.height, context.tip_height);

        if (beacon_height < 0 ) {
            return Contribution{};
        }

        assert(beacon_height <= context.tip_height);

        const auto beacon_age_scale =
            1.0 - AgeScale(beacon_height, context.tip_height, context.new_coin_maturity);

        assert(beacon_age_scale >= 0);
        assert(beacon_age_scale <= 1.01);

        Contribution c;

        //We compute both the linear and sublinear versions of the contribution.
        //This is done because there are two pools of selections evenly split
        //between stake oriented and growth oriented engagements.
        c.value = (beacon_age_scale * aged_balance.second) + aged_balance.first;
        c.sub = boost::multiprecision::log(ContributionAmount{1.0} + c.value);

        assert(c.value >= 0);
        assert(c.value <= aged_balance.second);
        assert(c.sub >= 0);

        return c;
    }

    struct Node 
    {
        char address_type;
        referral::Address address;
        Children children;
        SubtreeContribution contribution;
    };

    using NodeStack = std::stack<Node>;
    
    using AddressQueue = std::deque<AddressPair>;

    /**
     * Computes the subtree contribution rooted at the address specified.
     * This algorithm computes the subtree contribution by doing a post order
     * traversal of the ambassador tree.
     *
     * TODO: Implement parallel version, maybe Coffman–Graham algorithm.
     */
    SubtreeContribution ContributionSubtreeIter(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        const auto c = context.subtree_contribution.find(address);

        if (c != context.subtree_contribution.end()) {
            return c->second;
        }

        const auto& root_entrant = context.GetEntrant(address);

        SubtreeContribution contribution;

        NodeStack ns;
        ns.push({
                root_entrant.address_type,
                address,
                root_entrant.children,
                {}});

        while (!ns.empty()) {
            auto& n = ns.top();
            n.contribution.value += contribution.value;
            n.contribution.sub += contribution.sub;
            n.contribution.tree_size += contribution.tree_size;

            if (n.children.empty()) {
                const auto& c = context.GetEntrant(n.address).contribution;
                n.contribution.value += c.value;
                n.contribution.sub += c.sub;
                n.contribution.tree_size++;

                assert(n.contribution.value >= 0);
                assert(n.contribution.sub >= 0);

                contribution = n.contribution;
                context.subtree_contribution[n.address] = n.contribution;

                ns.pop();

            } else {
                const auto child_address = n.children.back();
                n.children.pop_back();

                contribution.value = 0;
                contribution.sub = 0;
                contribution.tree_size = 0;

                const auto& child_entrant = context.GetEntrant(child_address);

                ns.push({
                        child_entrant.address_type,
                        child_address,
                        child_entrant.children,
                        {}});
            }
        }

        return context.subtree_contribution[address];
    }

    ContributionAmount GetValue(const SubtreeContribution& t)
    {
        return t.value;
    }

    ContributionAmount GetSubValue(const SubtreeContribution& t)
    {
        return t.sub;
    }

    struct WeightedScores
    {
        ContributionAmount value;
        ContributionAmount sub;
        size_t tree_size;
    };

    WeightedScores WeightedScore(
            CGSContext& context,
            const char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        assert(context.tree_contribution.value > 0);
        assert(context.tree_contribution.sub > 0);

        const auto subtree_contribution =
                ContributionSubtreeIter(
                    context,
                    address_type,
                    address,
                    db);
        assert(subtree_contribution.value >= 0);
        assert(subtree_contribution.value <= context.tree_contribution.value);
        assert(subtree_contribution.sub <= context.tree_contribution.sub);

        WeightedScores score;
        score.value = ConvexF<ContributionAmount>(
                subtree_contribution.value / context.tree_contribution.value,
                context.B,
                context.S);

        score.sub = ConvexF<ContributionAmount>(
                subtree_contribution.sub / context.tree_contribution.sub,
                context.B,
                context.S);

        score.tree_size = subtree_contribution.tree_size;
        
        assert(score.value >= 0);
        assert(score.sub >= 0);
        assert(score.tree_size > 0);
        return score;
    }

    struct ExpectedValues
    {
        ContributionAmount value;
        ContributionAmount sub;
        size_t tree_size;
    };

    ExpectedValues ExpectedValue(
            CGSContext& context,
            const CachedEntrant& entrant,
            referral::ReferralsViewCache& db)
    {
        //this case can occur on regtest if there is not enough data.
        if (context.tree_contribution.value == 0) {
            return {0, 0, 0};
        }

        assert(context.tree_contribution.value > 0);
        assert(context.tree_contribution.sub > 0);

        auto expected_value = WeightedScore(
                context,
                entrant.address_type,
                entrant.address,
                db);

        assert(expected_value.value >= 0);
        assert(expected_value.sub >= 0);

        for (const auto& c:  entrant.children) {
            const auto& child_entrant = context.GetEntrant(c);
            auto child_score = WeightedScore(
                    context,
                    child_entrant.address_type,
                    c,
                    db);

            assert(child_score.value >= 0);
            assert(child_score.sub >= 0);
            expected_value.value -= child_score.value;
            expected_value.sub -= child_score.sub;
        }

        assert(expected_value.value >= 0);
        assert(expected_value.sub >= 0);
        return { 
            expected_value.value,
                expected_value.sub,
                expected_value.tree_size
        };
    }

    Entrant ComputeCGS(
            CGSContext& context,
            const CachedEntrant& entrant,
            referral::ReferralsViewCache& db)
    {
        auto expected_value = ExpectedValue(
                context,
                entrant,
                db);

        const ContributionAmount cgs = context.tree_contribution.value * expected_value.value;
        const ContributionAmount sub_cgs = context.tree_contribution.sub * expected_value.sub;

        assert(cgs >= 0);
        assert(sub_cgs >= 0);

        auto floored_cgs = cgs.convert_to<CAmount>();
        auto floored_sub_cgs = sub_cgs.convert_to<CAmount>();

        const auto& balance = entrant.balances;

        return Entrant{
                entrant.address_type,
                entrant.address,
                balance.second,
                balance.first,
                floored_cgs,
                floored_sub_cgs,
                entrant.height,
                entrant.children.size(),
                expected_value.tree_size
        };
    }

    void ComputeAges(CGSContext& context) {
        std::vector<std::future<void>> jobs;
        jobs.reserve(context.entrants.size() / BATCH_SIZE);
        for(size_t b = 0; b < context.entrants.size(); b+=BATCH_SIZE) {
            jobs.push_back(
                    cgs_pool.push([b, &context](int id) {
                        const auto end = std::min(context.entrants.size(), b + BATCH_SIZE);
                        for(size_t i = b; i < end; i++) {
                            auto& e = context.entrants[i];
                            e.balances = AgedBalance(
                                    context.tip_height,
                                    e.coins,
                                    context.coin_maturity,
                                    BalanceDecay);
                        }
                    }));
        }
        for(auto& j : jobs) {
            j.wait();
        }
    }

    void PrefillContributionsAndHeights(
            CGSContext& context,
            const char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db) {

        AddressQueue q;
        q.push_back(std::make_pair(address_type, address));
        while(!q.empty()) {
            const auto p = q.front();
            q.pop_front();

            const auto height = GetReferralHeight(db, p.second);

            const auto& entrant = context.AddEntrant(
                    p.first,
                    p.second, 
                    height,
                    db.GetChildren(p.second));

            for(const auto& c : entrant.children) {
                const auto maybe_ref = db.GetReferral(c);
                if (!maybe_ref) {
                    continue;
                }

                q.push_back(std::make_pair(maybe_ref->addressType, maybe_ref->GetAddress()));
            }

        }
    }

    void ComputeAllContributions(
            CGSContext& context,
            referral::ReferralsViewCache& db) {

        std::vector<std::future<void>> jobs;
        jobs.reserve(context.entrants.size() / BATCH_SIZE);
        for(size_t b = 0; b < context.entrants.size(); b+=BATCH_SIZE) {
            jobs.push_back(
                    cgs_pool.push([b, &context, &db](int id) {
                        const auto end = std::min(context.entrants.size(), b + BATCH_SIZE);
                        for(size_t i = b; i < end; i++) {
                            auto& e = context.entrants[i];
                            e.contribution = ContributionNode(context, e, db);
                        }
                    }));
        }
        for(auto& j : jobs) {
            j.wait();
        }
    }

    void ComputeAllScores(
            CGSContext& context,
            referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            Entrants& entrants)
    {
        referral::AddressANVs anv_entrants;
        db.GetAllRewardableANVs(params, NO_GENESIS, anv_entrants);

        std::vector<std::future<Entrants>> jobs;
        jobs.reserve(anv_entrants.size() / BATCH_SIZE);

        for(size_t b = 0; b < anv_entrants.size(); b+=BATCH_SIZE) {
            jobs.push_back(
                    cgs_pool.push([b, &anv_entrants, &context, &db](int id) {
                        const auto end = std::min(anv_entrants.size(), b + BATCH_SIZE);
                        Entrants es;
                        es.reserve(end - b);
                        for(size_t i = b; i < end; i++) {
                            const auto& a = anv_entrants[i];
                            const auto& e = context.GetEntrant(a.address);
                            es.emplace_back(ComputeCGS(context, e, db));
                        }
                        return es;
                    }));
        }

        entrants.reserve(anv_entrants.size()*BATCH_SIZE);

        for(auto& j : jobs) {
            auto es = j.get();
            entrants.insert(entrants.end(), es.begin(), es.end());
        }
    }

    void GetAllRewardableEntrants(
            CGSContext& context,
            referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(height >= 0);

        context.tip_height = height;
        context.coin_maturity = params.pog2_coin_maturity;
        context.new_coin_maturity = params.pog2_new_coin_maturity;
        context.B = params.pog2_convex_b;
        context.S = params.pog2_convex_s;
        PrefillContributionsAndHeights(
                context,
                2,
                params.genesis_address,
                db);
        GetAllCoins(context, height);
        ComputeAges(context);

        ComputeAllContributions(context, db);
        context.tree_contribution = ContributionSubtreeIter(context, 2, params.genesis_address, db);

        ComputeAllScores(context, db, params, entrants);
    }

    CachedEntrant& CGSContext::AddEntrant(
            char address_type,
            const referral::Address& address,
            int height,
            const Children& children)
    {

        CachedEntrant e;
        e.address = address;
        e.address_type = address_type;
        e.height = height;
        e.children = children;

        entrants.emplace_back(e);
        auto ei = entrant_idx.insert(std::make_pair(address, entrants.size() - 1));
        assert(ei.second);
        return entrants.back();
    }

    CachedEntrant& CGSContext::GetEntrant(const referral::Address& a)
    {
        auto p = entrant_idx.find(a);
        assert(p != entrant_idx.end());
        return entrants[p->second];
    }

    const CachedEntrant& CGSContext::GetEntrant(const referral::Address& a) const
    {
        const auto p = entrant_idx.find(a);
        assert(p != entrant_idx.end());
        return entrants[p->second];
    }

} // namespace pog2
