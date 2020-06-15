// Copyright (c) 2017-2020 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ref_verify.h"

#include "primitives/referral.h"
#include "referrals.h"
#include "validation.h"

namespace referral
{
    bool CheckReferral(
            const Referral& referral,
            bool normalize_alias,
            CValidationState& state)
    {
        // Basic checks that don't depend on any context
        if (referral.GetAddress().IsNull()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-ref-no-address");
        }

        // Check referral pubkey and signature
        if (!referral.pubkey.IsValid()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-ref-invalid-pubkey");
        } else {
            if (referral.signature.empty()) {
                return state.DoS(100, false, REJECT_INVALID, "bad-ref-sig-empty");
            }
        }

        if (referral.version >= Referral::INVITE_VERSION 
                && !CheckReferralAlias(referral.alias, normalize_alias)) {

            return state.DoS(100, false, REJECT_INVALID, "bad-ref-invalid-alias");
        }

        return true;
    }

} //namespace referral
