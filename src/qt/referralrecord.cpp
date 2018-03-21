// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referralrecord.h"
#include "refmempool.h"

#include "base58.h"
#include "validation.h"
#include "wallet/wallet.h"

#include <stdint.h>

void ReferralRecord::UpdateStatus(const referral::ReferralRef& ref)
{
    assert(ref);
    AssertLockHeld(cs_main);

    // Determine referral status
    if(status != Status::Pending)
      return;
    if(CheckAddressConfirmed(ref->GetAddress(), ref->addressType, true)) {
      status = Status::Confirmed;
    }
}

void ReferralRecord::DeclineRecord() {
    status = Status::Declined;
}

// temporary
QString ReferralRecord::DisplayString() const
{
    return alias.length() > 0 ?
        QString::fromStdString(address) + " (" + QString::fromStdString(alias) + ")":
        QString::fromStdString(address);
}

// temporary
QString ReferralRecord::StatusString() const
{
    switch(status)
    {
    case Status::Pending:
        return QString::fromStdString("Pending");
    case Status::Confirmed:
        return QString::fromStdString("Confirmed");
    case Status::Declined:
        return QString::fromStdString("Declined");
    }
    return "";
}

bool ShowReferral(const referral::ReferralRef& ref)
{
    assert(ref);
    return ref->addressType == 1;
}

ReferralRecord DecomposeReferral(const referral::ReferralRef ref, uint64_t time_received)
{
    assert(ref);

    const CMeritAddress meritAddress{ref->addressType, ref->GetAddress()};
    return ReferralRecord{ref->GetHash(), static_cast<qint64>(time_received), meritAddress.ToString(), ref->alias};
}

/*
 * Decompose CWallet referral to model referral records.
 */
ReferralRecord DecomposeReferral(const referral::ReferralTx &rtx)
{
    return DecomposeReferral(rtx.GetReferral(), rtx.nTimeReceived);
}

ReferralRecord DecomposeReferral(const referral::RefMemPoolEntry &e)
{
    return DecomposeReferral(e.GetSharedEntryValue(), static_cast<qint64>(e.GetTime()));
}

