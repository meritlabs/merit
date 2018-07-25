// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog2/cgs.h"
#include "addressindex.h"
#include "validation.h"
#include "referrals.h"

#include <stack>
#include <deque>
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

    double AgedNetworkSize(
            int tip_height,
            const Consensus::Params& params, 
            referral::ReferralsViewCache& db,
            const referral::AddressANVs& all_entrants) {

        double aged_network_size = 0.0;
        for(const auto& e: all_entrants) {
            const auto height = GetReferralHeight(db, e.address);
            const auto scaled_size = 1.0 - AgeScale(height, tip_height, params.pog2_coin_maturity);   
            aged_network_size += scaled_size;   
        }

        return aged_network_size;
    }

    double AgedNetworkSize(
            int tip_height,
            const Consensus::Params& params, 
            referral::ReferralsViewCache& db) {

        referral::AddressANVs all_entrants;
        db.GetAllRewardableANVs(params, NO_GENESIS, all_entrants);
        return AgedNetworkSize(tip_height, params, db, all_entrants);
    }


    void GetAllRewardableEntrants(
            referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(prefviewcache);

        referral::AddressANVs anv_entrants;

        db.GetAllRewardableANVs(params, NO_GENESIS, anv_entrants);

        const auto aged_network_size = AgedNetworkSize(height, params, db, anv_entrants);

        entrants.resize(anv_entrants.size());

        CGSContext context;
        std::transform(anv_entrants.begin(), anv_entrants.end(), entrants.begin(),
                [height, &db, &context, &params, aged_network_size](const referral::AddressANV& a) {
                    auto entrant = ComputeCGS(
                            context,
                            aged_network_size,
                            height,
                            params.pog2_coin_maturity,
                            params.pog2_child_coin_maturity,
                            a.address_type,
                            a.address,
                            db);

                    auto height = GetReferralHeight(db, a.address);
                    assert(height >= 0);
                    entrant.beacon_height = height;
                    return entrant;
                });
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

    BalancePair GetAgedBalance(
            CGSContext& context,
            int tip_height,
            int maturity,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.balances.find(address);

        if(cached_balance == context.balances.end()) {
            auto coins = GetCoins(tip_height, address_type, address);
            auto balance = AgedBalance(tip_height, coins, maturity, SelfAgedBalance);
            context.balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }

    BalancePair GetChildAgedBalance(
            CGSContext& context,
            int tip_height,
            int maturity,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.child_balances.find(address);

        if(cached_balance == context.child_balances.end()) {
            auto coins = GetCoins(tip_height, address_type, address, false);
            auto balance = AgedBalance(tip_height, coins, maturity, CChildAgedBalance);
            context.child_balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }

    Entrant ComputeCGS(
            CGSContext& context,
            double total_aged_network,
            int tip_height,
            int coin_maturity,
            int child_coin_maturity,
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
                ComputeCGS(
                        context,
                        total_aged_network,
                        tip_height,
                        coin_maturity,
                        child_coin_maturity,
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
                    tip_height,
                    child_coin_maturity,
                    c.address_type,
                    c.address);

            const auto child_height = GetReferralHeight(db, c.address);
            const auto child_age_scale = 1.0 - AgeScale(child_height, tip_height, child_coin_maturity);

            const double ccgs =  (child_age_scale * child_balance.first) + c.cgs;
            assert(ccgs >= 0);

            child_cgs += ccgs;
            assert(child_cgs >= 0);
            network_size += c.network_size;
        }

        const auto balance_pair = GetAgedBalance(
                context,
                tip_height,
                coin_maturity,
                address_type,
                address);

        assert(balance_pair.first >= 0);
        assert(balance_pair.second >= 0);

        CAmount self_cgs = balance_pair.first;
        assert(self_cgs >= 0);

        const double S[] = {0.10, 0.90};
        const double cgs = ((S[0] * self_cgs) + (S[1] * child_cgs));

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
                S[0]*self_cgs,
                S[1]*child_cgs,
                (S[0]*self_cgs) / cgs,
                (S[1]*child_cgs) / cgs,
        };

        context.entrant_cgs[address] = root;
        return root;
    }

} // namespace pog2
