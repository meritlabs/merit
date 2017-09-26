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
    if (referral.m_pubKeyId.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-no-pubkey");
    }

    if (referral.m_codeHash.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ref-code-empty");
    }

    return true;
}

} //namespace referral
