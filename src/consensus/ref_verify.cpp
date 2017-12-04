// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ref_verify.h"

#include "referrals.h"
#include "primitives/referral.h"
#include "validation.h"

namespace referral
{

bool CheckReferral(const Referral& referral, CValidationState &state)
{
    // Basic checks that don't depend on any context
    if (referral.address.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-no-address");
    }

    // TODO: check parentAddress not empty if not genesis
    // if (referral.parentAddress.IsNull()) {
    //     return state.DoS(100, false, REJECT_INVALID, "bad-ref-code-empty");
    // }

    // if (referral.signature.empty()) {
    //     return state.DoS(100, false, REJECT_INVALID, "bad-ref-signature-empty");
    // }

    return true;
}

} //namespace referral
