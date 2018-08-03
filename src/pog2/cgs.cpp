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
            bool filter_coinbase) {
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

    BalancePair CReverseAgedBalance(int tip_height, const Coin& c, int maturity) {
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

        auto aged_balance = 
            std::accumulate(balances.begin(), balances.end(), double{0}, 
                [](double amount, const BalancePair& b) {
                    return amount + b.first;
               });

        auto balance =std::accumulate(balances.begin(), balances.end(), double{0}, 
                [](double amount, const BalancePair& b) {
                    return amount + b.second;
                });
        
        assert(aged_balance <= balance);
        return BalancePair{aged_balance, balance};
    }

    BalancePair GetAgedBalance(
            CGSContext& context,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.balances.find(address);

        if(cached_balance == context.balances.end()) {
            auto coins = GetCoins(context.tip_height, address_type, address, false);
            auto balance = AgedBalance(context.tip_height, coins, context.coin_maturity, SelfAgedBalance);
            context.balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }


    BalancePair GetReverseAgedBalance(
            CGSContext& context,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.child_balances.find(address);

        if(cached_balance == context.child_balances.end()) {
            auto coins = GetCoins(context.tip_height, address_type, address, false);
            auto balance = AgedBalance(context.tip_height, coins, context.new_coin_maturity, CReverseAgedBalance);
            context.child_balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }

    //Convex function with property that if C0 > C1 and you have A within [0, 1] then 
    //ConvexF(C0 + A) - ConvexF(C0) > ConvexF(C1 + A) - ConvexF(C1);
    //See: Lottery Trees: Motivational Deployment of Networked Systems
    template <class V>
    V ConvexF(V c, V B, V S) { 
        assert(c >= V{0});
        assert(c <= V{1.01});
        assert(B >= V{0});
        assert(B <= V{1.01});
        assert(S >= V{0});
        assert(S <= V{1.01});

        return (B*c) + ((V{1} - B)*boost::multiprecision::pow(c, V{1} + S));
    }

    Contribution ContributionNode(
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

        const auto fresh = GetReverseAgedBalance(
                context,
                address_type,
                address);

        const auto height = std::min(GetReferralHeight(db, address), context.tip_height);
        if(height < 0 ) {
            return Contribution{0.0, 0.0};
        }

        assert(height <= context.tip_height);

        const auto age_scale = 1.0 - AgeScale(height, context.tip_height, context.new_coin_maturity);

        assert(age_scale >= 0);
        assert(age_scale <= 1.01);

        Contribution c;
        c.value = (age_scale * fresh.second) + old.first;
        c.log = boost::multiprecision::log1p(c.value);

        assert(c.value >= 0);
        assert(c.value <= fresh.second);

        context.contribution[address] = c;
        return c;
    }

    using Children = std::vector<referral::Address>;
    struct Node 
    {
        char address_type;
        referral::Address address;
        Children children;
        SubtreeContribution contribution;
    };
    using Nodes = std::stack<Node>;

    /**
     * Computes the subtree contribution rooted at the address specified.
     * This algorithm computes the subtree contribution by doing a post order
     * traversal of the ambassador tree.
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

        const auto children = db.GetChildren(address);
        const auto maybe_ref = db.GetReferral(address);

        if(!maybe_ref) {
            return {0, 0, 0};
        }

        SubtreeContribution contribution;
        contribution.value = 0;
        contribution.log = 0;
        contribution.tree_size = 0;

        Nodes ns;
        ns.push({
                maybe_ref->addressType,
                maybe_ref->GetAddress(),
                children,
                0,
                0});

        while(!ns.empty()) {
            auto& n = ns.top();
            n.contribution.value += contribution.value;
            n.contribution.log += contribution.log;
            n.contribution.tree_size += contribution.tree_size;

            if(n.children.empty()) {
                auto c = ContributionNode(
                        context,
                        n.address_type,
                        n.address,
                        db);

                n.contribution.value += c.value;
                n.contribution.log += c.log;
                n.contribution.tree_size++;

                contribution = n.contribution;
                context.subtree_contribution[n.address] = n.contribution;

                ns.pop();

            } else {
                const auto child_address = n.children.back();
                n.children.pop_back();

                contribution.value = 0;
                contribution.log = 0;
                contribution.tree_size = 0;

                const auto children = db.GetChildren(child_address);
                const auto maybe_ref = db.GetReferral(child_address);

                if(maybe_ref) {
                    ns.push({
                            maybe_ref->addressType,
                            maybe_ref->GetAddress(),
                            children,
                            {0,0, 0}});
                }
            }
        }

        return context.subtree_contribution[address];
    }

    ContributionAmount GetValue(const SubtreeContribution& t)
    {
        return t.value;
    }

    ContributionAmount GetLog(const SubtreeContribution& t)
    {
        return t.value;
    }

    template <class ValueFunc>
    ContributionAmount WeightedScore(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db,
            ValueFunc value)
    {
        assert(context.tree_contribution.value >= 0);
        
        const auto subtree_contribution = value(
                ContributionSubtreeIter(
                    context,
                    address_type,
                    address,
                    db));

        assert(subtree_contribution >= 0);
        assert(subtree_contribution <= value(context.tree_contribution));

        return ConvexF<ContributionAmount>(
                subtree_contribution / value(context.tree_contribution),
                context.B,
                context.S);
    }

    template <class ValueFunc>
        ContributionAmount ExpectedValue(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db,
            ValueFunc value)
        {
            if(value(context.tree_contribution) == 0) {
                return 0;
            }

            ContributionAmount child_scores = 0;
            const auto children = db.GetChildren(address);

            for(const auto& c:  children) {
                auto maybe_ref = db.GetReferral(c);
                if(!maybe_ref) {
                    continue;
                }

                child_scores += WeightedScore(
                        context,
                        maybe_ref->addressType,
                        maybe_ref->GetAddress(),
                        db, value);
            }

            return WeightedScore(
                    context,
                    address_type,
                    address,
                    db, value) - child_scores;
        }

    Entrant ComputeCGS(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db)
    {
        context.B = 0.1;
        context.S = 0.03;

        const auto balance = GetAgedBalance(
                context,
                address_type,
                address);

        const auto children = db.GetChildren(address);
        const auto height = GetReferralHeight(db, address);
        const auto cgs = context.tree_contribution.value * ExpectedValue(
                context,
                address_type,
                address,
                db,
                GetValue);

        const auto log_cgs = context.tree_contribution.log * ExpectedValue(
                context,
                address_type,
                address,
                db,
                GetLog);

        const auto contribution =
            ContributionNode(context, address_type, address, db);

        const auto subtree_contribution =
            ContributionSubtreeIter(context, address_type, address, db);

        return Entrant{
            address_type,
                address,
                balance.second,
                static_cast<CAmount>(balance.first),
                static_cast<CAmount>(cgs),
                static_cast<CAmount>(log_cgs),
                1,
                children.size(),
                subtree_contribution.tree_size,
                height,
                static_cast<double>(contribution.value),
                std::max(0.0, static_cast<double>(subtree_contribution.value) - static_cast<double>(contribution.value))
        };
    }

    void GetAllRewardableEntrants(
            CGSContext& context,
            referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(height >= 0);

        referral::AddressANVs anv_entrants;

        db.GetAllRewardableANVs(params, NO_GENESIS, anv_entrants);

        entrants.resize(anv_entrants.size());

        context.tip_height = height;
        context.coin_maturity = params.pog2_coin_maturity;
        context.new_coin_maturity = params.pog2_new_coin_maturity;
        context.tree_contribution = ContributionSubtreeIter(context, 2, params.genesis_address, db);

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

} // namespace pog2
