// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ref_verify.h"

#include "referrals.h"
#include "primitives/referral.h"
#include "validation.h"

bool CheckReferral(const Referral& referral, const ReferralsViewCache& refView, CValidationState &state)
{
    // Basic checks that don't depend on any context

    // check that referral code is not empty and this referral is not genesis referral
    if (!referral.m_previousReferral.IsNull() && !refView.ReferralCodeExists(referral.m_previousReferral)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-prevref-invalid");
    }

    if (referral.m_pubKeyId.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-no-pubkey");
    }

    if (referral.m_codeHash.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-code-empty");
    }

    return true;
}
