// Copyright (c) 2017-2019 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/anv.h"
#include <stack>

namespace pog 
{
    /**
     * This version simply pulls the ANV from the DB. ReferralsViewDB::UpdateANV
     * incrementally updates an ANV for an address and all parents.
     */
    referral::MaybeAddressANV ComputeANV(
            const referral::Address& address_id,
            const referral::ReferralsViewDB& db)
    {
        return db.GetANV(address_id);
    }

    referral::AddressANVs GetAllANVs(const referral::ReferralsViewDB& db)
    {
        return db.GetAllANVs();
    }

    void GetAllRewardableANVs(
            const referral::ReferralsViewCache& db,
            const Consensus::Params& params,
            int height,
            referral::AddressANVs& entrants,
            bool cached)
    {
        db.GetAllRewardableANVs(params, height, entrants, cached);
    }

    referral::AddressANVs GetANVs(
            const referral::Addresses& addresses,
            const referral::ReferralsViewDB& db)
    {
        referral::AddressANVs r;
        r.reserve(addresses.size());

        for(const auto& a : addresses) {
            if(auto maybe_anv = ComputeANV(a, db)) {
                r.push_back(*maybe_anv);
            }
        }

        assert(r.size() <= addresses.size());
        return r;
    }

} // namespace pog
