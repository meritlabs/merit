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

    void GetAllRewardableEntrants(
            const referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(prefviewcache);

        referral::AddressANVs anv_entrants;

        db.GetAllRewardableANVs(params, NO_GENESIS, anv_entrants);

        entrants.resize(anv_entrants.size());

        CGSContext context;
        std::transform(anv_entrants.begin(), anv_entrants.end(), entrants.begin(),
                [height, &db, &context](const referral::AddressANV& a) {
                    auto entrant = ComputeCGS(
                            context,
                            height,
                            a.address_type,
                            a.address,
                            db);

                    auto height = db.GetReferralHeight(a.address);
                    if (height < 0) {
                        auto beacon = db.GetReferral(a.address);
                        assert(beacon);

                        uint256 hashBlock;
                        CBlockIndex* pindex = nullptr;
                        
                        referral::ReferralRef beacon_out;
                        if(GetReferral(beacon->GetHash(), beacon_out, hashBlock, pindex)) {
                            assert(pindex);
                            height = pindex->nHeight;
                            if(height > 0) {
                                prefviewcache->SetReferralHeight(height, a.address);
                            }
                        }
                    }

                    assert(height >= 0);

                    entrant.beacon_height = height;
                    return entrant;
                });
    }

    struct Coin
    {
        int height;
        CAmount amount;
    };

    using Coins = std::vector<Coin>;
    using UnspentPair = std::pair<CAddressUnspentKey, CAddressUnspentValue>;

    Coins GetCoins(int height, char address_type, const referral::Address& address) {
        Coins cs;
        std::vector<UnspentPair> unspent;
        if (!GetAddressUnspent(address, address_type, false, unspent)) {
            return cs;
        }

        cs.reserve(unspent.size());
        for(const auto& p : unspent) {
            if(p.first.type == 0 || p.first.isInvite) {
                continue;
            }
            assert(p.second.satoshis >= 0);

            cs.push_back({std::min(p.second.blockHeight, height), p.second.satoshis});
        }

        return cs;
    }

    double Age(int height, const Coin& c) {
        assert(height >= 0);
        assert(c.height <= height);

        return height - c.height;
    }

    const double ONE_DAY = 24*60;
    const double TWO_DAYS = 2 * ONE_DAY;
    double AgeScale(int height, const Coin& c) {
        assert(height >= 0);
        assert(c.height <= height);

        double age = Age(height, c) / TWO_DAYS;
        assert(age >= 0);

        double scale =  1.0 - (1.0 / (std::pow(age, 2) + 1.0));

        assert(scale >= 0);
        assert(scale <= 1.001);
        return scale;
    }

    BalancePair AgedBalance(int height, const Coin& c) {
        assert(height >= 0);
        assert(c.height <= height);
        assert(c.amount >= 0);

        double age_scale = AgeScale(height, c);
        CAmount amount = std::floor(age_scale * c.amount);

        assert(amount >= 0);
        assert(amount <= c.amount);
        return BalancePair{amount, c.amount};
    }

    BalancePair AgedBalance(int height, const Coins& cs) {
        assert(height >= 0);

        BalancePairs balances(cs.size());
        std::transform(cs.begin(), cs.end(), balances.begin(),
                [height](const Coin& c) {
                    return AgedBalance(height, c);
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
        auto children = db.GetChildren(n.address);
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
            int height,
            char address_type,
            const referral::Address& address)
    {
        auto cached_balance = context.balances.find(address);

        if(cached_balance == context.balances.end()) {
            auto coins = GetCoins(height, address_type, address);
            auto balance = AgedBalance(height, coins);
            context.balances[address] = balance;
            return balance;
        } 
        return cached_balance->second;
    }


    Entrant ComputeCGS(
            CGSContext& context,
            int height,
            char address_type,
            const referral::Address& address,
            const referral::ReferralsViewCache& db)
    {
        auto balance_pair = GetAgedBalance(context, height, address_type, address);
        assert(balance_pair.first >= 0);
        assert(balance_pair.second >= 0);

        CAmount cgs = std::floor(balance_pair.first * 0.75);
        assert(cgs >= 0);

        EntrantQueue q;

        Entrant root{
            address_type,
                address,
                balance_pair.second,
                static_cast<CAmount>(balance_pair.first),
                cgs,
                1,
                0,
                0,
                0};
        PushChildren(db, root, q);

        while(!q.empty()) {
            auto n = q.front();
            q.pop_front();

            root.network_size++;

            auto entrant_balance = GetAgedBalance(
                    context,
                    height,
                    n.address_type,
                    n.address);

            assert(entrant_balance.first >=0);
            root.cgs += entrant_balance.first / n.level;

            PushChildren(db, n, q);
        }

        return root;
    }

} // namespace pog2
