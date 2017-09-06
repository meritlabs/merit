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
    if (!refView.ReferralCodeExists(referral.m_previousReferral)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-referral-prevref-not-exists");
    }

    if (refView.ReferralCodeExists(referral.m_codeHash)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-referral-already-exists");
    }

    if (referral.m_previousReferral.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-referral-empty");
    }

    if (referral.m_codeHash.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-referral-prevref-empty");
    }

    return true;
}
