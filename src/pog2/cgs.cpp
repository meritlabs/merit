// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog2/cgs.h"
#include "addressindex.h"
#include "validation.h"
#include "referrals.h"

#include <stack>
#include <deque>
#include <stack>
#include <numeric>

namespace pog2
{
    namespace
    {
        const int NO_GENESIS = 13500;
    }

    struct Coin
    {
        int height;
        CAmount amount;
    };

    using Coins = std::vector<Coin>;
    using UnspentPair = std::pair<CAddressUnspentKey, CAddressUnspentValue>;


    double Age(int height, int tip_height) {
        assert(tip_height >= 0);
        assert(height <= tip_height);

        return tip_height - height;
    }

    double AgeScale(int height, int tip_height, double maturity)
    {
        assert(tip_height >= 0);
        assert(height <= tip_height);

        const double maturity_scale = maturity / 4.0; //matures to about 97% at 4

        const auto age = Age(height, tip_height) / maturity_scale;
        assert(age >= 0);

        const auto age_scale =  1.0 - (1.0 / (std::pow(age, 2) + 1.0));

        assert(age_scale >= 0);
        assert(age_scale <= 1.001);
        return age_scale;
    }

    double AgeScale(const Coin& c, int tip_height, int maturity) {
        assert(tip_height >= 0);
        assert(c.height <= tip_height);
        return AgeScale(c.height, tip_height, maturity);
    }

    int GetReferralHeight(
            referral::ReferralsViewCache& db,
            const referral::Address& a
            )
    {
        auto height = db.GetReferralHeight(a);
        if (height < 0) {
            auto beacon = db.GetReferral(a);
            assert(beacon);

            uint256 hashBlock;
            CBlockIndex* pindex = nullptr;

            referral::ReferralRef beacon_out;
            if(GetReferral(beacon->GetHash(), beacon_out, hashBlock, pindex)) {
                assert(pindex);
                height = pindex->nHeight;
                if(height > 0) {
                    db.SetReferralHeight(height, a);
                }
            }
        }
        return height;
    }

    Coins GetCoins(
            int height,
            char address_type,
            const referral::Address& address,
            bool filter_coinbase = true) {
        Coins cs;
        std::vector<UnspentPair> unspent;
        if (!GetAddressUnspent(address, address_type, false, unspent)) {
            return cs;
        }

        cs.reserve(unspent.size());
        for(const auto& p : unspent) {
            if(p.first.type == 0 || p.first.isInvite || (filter_coinbase && p.first.isCoinbase)) {
                continue;
            }
            assert(p.second.satoshis >= 0);

            cs.push_back({std::min(p.second.blockHeight, height), p.second.satoshis});
        }

        return cs;
    }

    BalancePair SelfAgedBalance(int tip_height, const Coin& c, int maturity) {
        assert(tip_height >= 0);
        assert(c.height <= tip_height);
        assert(c.amount >= 0);

        const double age_scale = AgeScale(c, tip_height, maturity);
        CAmount amount = std::floor(age_scale * c.amount);

        assert(amount >= 0);
        assert(amount <= c.amount);
        return BalancePair{amount, c.amount};
    }

    BalancePair CChildAgedBalance(int tip_height, const Coin& c, int maturity) {
        assert(tip_height >= 0);
        assert(c.height <= tip_height);
        assert(c.amount >= 0);

        const double age_scale = 1.0 - AgeScale(c, tip_height, maturity);
        CAmount amount = std::floor(age_scale * c.amount);

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
        
        return BalancePair{
            std::accumulate(balances.begin(), balances.end(), double{0}, 
                [](double amount, const BalancePair& b) {
                    return amount + b.first;
                }),
            std::accumulate(balances.begin(), balances.end(), double{0}, 
                [](double amount, const BalancePair& b) {
                    return amount + b.second;
                })};
    }

    BalancePair GetAgedBalance(
            CGSContext& context,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.balances.find(address);

        if(cached_balance == context.balances.end()) {
            auto coins = GetCoins(context.tip_height, address_type, address);
            auto balance = AgedBalance(context.tip_height, coins, context.coin_maturity, SelfAgedBalance);
            context.balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }


    BalancePair GetChildAgedBalance(
            CGSContext& context,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.child_balances.find(address);

        if(cached_balance == context.child_balances.end()) {
            auto coins = GetCoins(context.tip_height, address_type, address, false);
            auto balance = AgedBalance(context.tip_height, coins, context.child_coin_maturity, CChildAgedBalance);
            context.child_balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }

    template <class V>
    V ConvexF(V c, V B, V S) { 
        return B*c + (V{1} - B)*boost::multiprecision::pow(c, V{1} + S);
    }

    template <class V>
    V ContributionNode(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        const auto n = context.contribution.find(address);
        if(n != context.contribution.end()) {
            return n->second;
        }

        const auto old = GetAgedBalance(
                context,
                address_type,
                address);

        const auto fresh = GetChildAgedBalance(
                context,
                address_type,
                address);

        const auto height = GetReferralHeight(db, address);
        const auto age_scale = 1.0 - AgeScale(height, context.tip_height, context.child_coin_maturity);

        const V contribution = 1_merit + ((age_scale * fresh.second) + old.first);
        context.contribution[address] = contribution;
        return contribution;
    }

    //TODO: Implement iterative form
    template< class V>
    std::pair<V, size_t> ContributionSubtree(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        const auto c = context.subtree_contribution.find(address);
        if (c != context.subtree_contribution.end()) {
            return c->second;
        }

        V contribution = 0;
        const auto children = db.GetChildren(address);

        size_t tree_size = 1;
        for(const auto& c:  children) {
            const auto maybe_ref = db.GetReferral(c);
            if(!maybe_ref) {
                continue;
            }

            const auto child_subtree = ContributionSubtree<V>(
                    context,
                    maybe_ref->addressType,
                    maybe_ref->GetAddress(),
                    db);

            contribution += child_subtree.first;
            tree_size += child_subtree.second;
        }

        contribution += ContributionNode<V>(
                context,
                address_type,
                address,
                db);

        const auto res = std::make_pair(contribution, tree_size);
        context.subtree_contribution[address] = res;
        return res;
    }

    template<class V>
    V WeightedScore(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        return ConvexF<V>(
                ContributionSubtree<V>(
                    context,
                    address_type,
                    address,
                    db).first / context.tree_contribution,
                context.B,
                context.S);
    }

    template<class V>
        V ExpectedValue(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
        {

            V child_scores = 0;
            const auto children = db.GetChildren(address);

            for(const auto& c:  children) {
                auto maybe_ref = db.GetReferral(c);
                if(!maybe_ref) {
                    continue;
                }

                child_scores += WeightedScore<V>(
                        context,
                        maybe_ref->addressType,
                        maybe_ref->GetAddress(),
                        db);
            }

            return WeightedScore<V>(
                    context,
                    address_type,
                    address,
                    db) - child_scores;
        }

    Entrant ComputeCGS1(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        context.B = 0.5;
        context.S = 0.16;

        const auto balance = GetAgedBalance(
                context,
                address_type,
                address);

        const auto children = db.GetChildren(address);
        const auto height = GetReferralHeight(db, address);
        const auto cgs = context.tree_contribution * ExpectedValue<BigFloat>(
                context,
                address_type,
                address,
                db);

        const auto contribution =
            ContributionNode<BigFloat>(context, address_type, address, db);

        const auto subtree_contribution =
            ContributionSubtree<BigFloat>(context, address_type, address, db);

        return Entrant{
            address_type,
                address,
                balance.second,
                balance.first,
                static_cast<CAmount>(cgs),
                1,
                children.size(),
                subtree_contribution.second,
                height,
                static_cast<double>(contribution),
                std::max(0.0, static_cast<double>(subtree_contribution.first) - static_cast<double>(contribution))
        };
    }

    void GetAllRewardableEntrants(
            CGSContext& context,
            referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(prefviewcache);

        referral::AddressANVs anv_entrants;

        db.GetAllRewardableANVs(params, NO_GENESIS, anv_entrants);

        entrants.resize(anv_entrants.size());

        context.tip_height = height;
        context.coin_maturity = params.pog2_coin_maturity;
        context.child_coin_maturity = params.pog2_child_coin_maturity;
        context.tree_contribution = ContributionSubtree<BigFloat>(context, 2, params.genesis_address, db).first;

        std::transform(anv_entrants.begin(), anv_entrants.end(), entrants.begin(),
                [height, &db, &context, &params](const referral::AddressANV& a) {
                    auto entrant = ComputeCGS(
                            context,
                            a.address_type,
                            a.address,
                            db);

                    auto height = GetReferralHeight(db, a.address);
                    assert(height >= 0);
                    entrant.beacon_height = height;
                    return entrant;
                });
    }

    using EntrantQueue = std::deque<Entrant>; 

    void PushChildren(
            const referral::ReferralsViewCache& db,
            Entrant& n,
            EntrantQueue& q) 
    {
        const auto children = db.GetChildren(n.address);
        n.children = children.size();
        for(const auto& address : children) {

            auto maybe_ref = db.GetReferral(address);
            if(maybe_ref) { 
                q.push_back({
                        maybe_ref->addressType,
                        maybe_ref->GetAddress(),
                        0,
                        0,
                        0,
                        n.level + 1,
                        0,
                        0,
                        0});
            }
        }
    }

    Entrant ComputeCGS0(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        auto cached_entrant = context.entrant_cgs.find(address);
        if(cached_entrant != context.entrant_cgs.end()) {
            return cached_entrant->second;
        }

        const auto child_addresses = db.GetChildren(address);
        Entrants cgs_children;

        for(const auto& child_address : child_addresses) {
            auto maybe_ref = db.GetReferral(child_address);

            auto c_cgs = 
                ComputeCGS0(
                        context,
                        maybe_ref->addressType,
                        maybe_ref->GetAddress(),
                        db);

            cgs_children.emplace_back(c_cgs);
        }
        
        double child_cgs = 0 ;
        size_t network_size = 1;
        for(const auto& c : cgs_children) { 

            const auto child_balance = GetChildAgedBalance(
                    context,
                    c.address_type,
                    c.address);

            const auto child_height = GetReferralHeight(db, c.address);
            const auto child_age_scale = 
                1.0 - AgeScale(
                    child_height,
                    context.tip_height,
                    context.child_coin_maturity);

            const double ccgs =  (child_age_scale * child_balance.first) + c.cgs;
            assert(ccgs >= 0);

            child_cgs += ccgs;
            assert(child_cgs >= 0);
            network_size += c.network_size;
        }

        const auto balance_pair = GetAgedBalance(
                context,
                address_type,
                address);

        assert(balance_pair.first >= 0);
        assert(balance_pair.second >= 0);

        CAmount self_cgs = balance_pair.first;
        assert(self_cgs >= 0);

        const double S = 0.50;
        const double w_self_cgs = S * self_cgs;
        const double w_child_cgs = std::min(1.0, self_cgs / (1 + (0.1 * child_cgs))) * (1.0 - S) * child_cgs;
        const double cgs =  w_self_cgs + w_child_cgs;

        Entrant root{
            address_type,
                address,
                balance_pair.second,
                static_cast<CAmount>(balance_pair.first),
                static_cast<CAmount>(std::ceil(cgs)),
                1,
                cgs_children.size(),
                network_size,
                GetReferralHeight(db, address),
                self_cgs,
                child_cgs
        };

        context.entrant_cgs[address] = root;
        return root;
    }

    Entrant ComputeCGS(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        return ComputeCGS1(
                context,
                address_type,
                address,
                db);
    }

    void TestChain() {
        BigFloat B = 0.5;
        BigFloat S = 0.16;

        std::vector<BigFloat> chain = {50.0, 25.0, 23.0, 2.0};
        std::vector<BigFloat> ev;

        BigFloat total = std::accumulate(chain.begin(), chain.end(), BigFloat{0}, [](BigFloat a, BigFloat v) { return a + v;});
        BigFloat contrib = 0.0;

        for(auto v : chain) {
            auto child_weight = ConvexF<BigFloat>(contrib / total, B, S);
            contrib += v;
            auto weight = ConvexF<BigFloat>(contrib / total, B, S);
            std::cerr << "c: " << child_weight << " w: " << weight << " d: " << weight-child_weight << std::endl;
            ev.push_back(weight - child_weight);
        }

        std::cout << "EXPECTED VAL " << ConvexF<BigFloat>(1, B, S) << std::endl;
        std::cout << "expected chain value: ";
        for(auto v : ev) std::cout << v << " ";
        std::cout << std::endl;
        BigFloat chain_total = std::accumulate(ev.begin(), ev.end(), BigFloat{0}, [](BigFloat a, BigFloat v) { return a + v;});
        std::cout << "CHAIN VAL: " << chain_total << std::endl;


    }
} // namespace pog2
