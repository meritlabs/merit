// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referralrecord.h"

#include "base58.h"
#include "validation.h"
#include "wallet/wallet.h"

#include <stdint.h>

/*
 * Decompose CWallet referral to model referral records.
 */
ReferralRecord ReferralRecord::decomposeReferral(const CWallet *wallet, referral::ReferralTx &rtx)
{
    const referral::ReferralRef pref{rtx.GetReferral()};
    assert(pref);

    const uint160 address{pref->GetAddress()};
    const std::string alias{pref->alias};
    const CMeritAddress meritAddress{pref->addressType, address};
    
    if(alias.length() > 0) {
      return ReferralRecord(rtx.nTimeReceived, meritAddress.ToString(), alias);
    }
    return ReferralRecord(rtx.nTimeReceived, meritAddress.ToString());
}

void ReferralRecord::updateStatus(referral::ReferralTx &rtx)
{
    AssertLockHeld(cs_main);
    // Determine referral status
    const referral::ReferralRef pref{rtx.GetReferral()};
    assert(pref);
    if(status.status != ReferralStatus::Pending)
      return;
    if(CheckAddressConfirmed(pref->GetAddress(), pref->addressType, true)) {
      status.status = ReferralStatus::Confirmed;
    }
}

bool ReferralRecord::showReferral(referral::ReferralTx &rtx)
{
    const referral::ReferralRef pref{rtx.GetReferral()};
    assert(pref);
    return pref->addressType == 1;
}