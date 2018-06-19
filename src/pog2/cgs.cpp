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

    void GetAllRewardableEntrants(
            const referral::ReferralsViewDB& db,
            const Consensus::Params& params,
            int height,
            Entrants& entrants)
    {
        assert(prefviewcache);

        referral::AddressANVs anv_entrants;
        db.GetAllRewardableANVs(params, height, anv_entrants);

        entrants.resize(anv_entrants.size());

        std::transform(anv_entrants.begin(), anv_entrants.end(), entrants.begin(),
                [height, &db](const referral::AddressANV& a) {
                    auto entrant = ComputeCGS(
                            height,
                            a.address_type,
                            a.address,
                            db);

                    auto height = prefviewcache->GetReferralHeight(a.address);
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

                    assert(height > 0);

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

    Coins GetCoins(char address_type, const referral::Address& address) {
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

            cs.push_back({p.second.blockHeight, p.second.satoshis});
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
        assert(scale <= 1);
        return scale;
    }

    double AgedBalance(int height, const Coin& c) {
        assert(height >= 0);
        assert(c.height <= height);
        assert(c.amount >= 0);

        double age_scale = AgeScale(height, c);
        CAmount amount = std::floor(age_scale * c.amount);

        assert(amount >= 0);
        assert(amount <= c.amount);
        return amount;
    }

    double AgedBalance(int height, const Coins& cs) {
        assert(height >= 0);

        return std::accumulate(cs.begin(), cs.end(), double{0}, 
                [height](double amount, const Coin& c) {
                    return amount + AgedBalance(height, c);
                });
    }

    using EntrantQueue = std::deque<Entrant>; 

    void PushChildren(
            const referral::ReferralsViewDB& db,
            Entrant& n,
            EntrantQueue& q) 
    {
        auto children = db.GetChildren(n.address);
        n.children = children.size();
        for(const auto& address : children) {

            auto maybe_ref = db.GetReferral(address);
            if(!maybe_ref) { 
                continue;
            }

            q.push_back({
                    maybe_ref->addressType,
                    maybe_ref->GetAddress(),
                    0,
                    n.level + 1,
                    0,
                    0});
        }
    }

    Entrant ComputeCGS(
            int height,
            char address_type,
            const referral::Address& address,
            const referral::ReferralsViewDB& db)
    {
        auto coins = GetCoins(address_type, address);
        auto balance = AgedBalance(height, coins);
        assert(balance >= 0);

        CAmount cgs = std::floor(balance * 0.75);
        assert(cgs >= 0);

        EntrantQueue q;

        Entrant root{address_type, address, cgs, 1, 0};
        PushChildren(db, root, q);

        while(!q.empty()) {
            auto n = q.front();
            q.pop_front();

            root.network_size++;

            auto entrant_coins = GetCoins(n.address_type, n.address);
            auto entrant_balance = AgedBalance(height, entrant_coins);
            assert(entrant_balance >=0);
            root.cgs += entrant_balance / n.level;

            PushChildren(db, n, q);
        }

        return root;
    }

} // namespace pog2
