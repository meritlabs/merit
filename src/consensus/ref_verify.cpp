// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ref_verify.h"

#include "primitives/referral.h"
#include "referrals.h"
#include "validation.h"

namespace referral
{
bool CheckReferral(const Referral& referral, CValidationState& state)
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

    if (referral.version >= Referral::INVITE_VERSION && referral.tag.size() > MAX_TAG_LENGTH) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-invalid-tag");
    }

    return true;
}

} //namespace referral
