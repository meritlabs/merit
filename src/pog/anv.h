// Copyright (c) 2017-2019 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_ANV_H
#define MERIT_POG_ANV_H

#include "primitives/referral.h"
#include "consensus/params.h"
#include "refdb.h"
#include "referrals.h"

#include <vector>

namespace pog 
{
    /**
     * Aggregate Network Value is computed by walking the referral tree down from the 
     * key_id specified and aggregating each child's ANV.
     */
    referral::MaybeAddressANV ComputeANV(
            const referral::Address&,
            const referral::ReferralsViewDB&);

    referral::AddressANVs GetANVs(
            const referral::Addresses& addresses,
            const referral::ReferralsViewDB&);

    referral::AddressANVs GetAllANVs(const referral::ReferralsViewDB&);

    void GetAllRewardableANVs(
            const referral::ReferralsViewCache&,
            const Consensus::Params&,
            int height,
            referral::AddressANVs&, 
            bool cached = false);

} // namespace pog

#endif //MERIT_POG_ANV_H
