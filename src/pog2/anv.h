// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_ANV2_H
#define MERIT_POG_ANV2_H

#include "primitives/referral.h"
#include "consensus/params.h"
#include "refdb.h"

#include <vector>

namespace pog2
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
            const referral::ReferralsViewDB&,
            const Consensus::Params&,
            int height,
            referral::AddressANVs&);

} // namespace pog2

#endif //MERIT_POG2_ANV_H
