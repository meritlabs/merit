// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_REFERRALRECORD_H
#define MERIT_QT_REFERRALRECORD_H

#include "amount.h"
#include "uint256.h"
#include "wallet/wallet.h"

#include <QList>
#include <QString>

class CWallet;
class ReferralTx;

/** UI model for referral status. The referral status is the part of a referral that will change over time.
 */
class ReferralStatus
{
public:
    ReferralStatus():
        status(Pending)
    {
    }

    enum Status {
        Pending,    // referral which needs an inviteTx to confirm
        Confirmed,  // referral which has been confirmed
        Declined,   // user refused to confirm this referral
    };

    Status status;
};

/** UI model for a referral.
 */
class ReferralRecord
{
public:
    ReferralRecord():
        hash(), time(0), address(""), alias("")
    {
    }

    ReferralRecord(uint256 _hash, qint64 _time, std::string _address, std::string _alias = ""):
        hash(_hash), time(_time), address(_address), alias(_alias)
    {
    }

    // addressType other than 1 means address is a script, no need to show those beacons here
    static bool showReferral(referral::ReferralTx &rtx);
    static ReferralRecord decomposeReferral(const CWallet *wallet, referral::ReferralTx &rtx);

    /** @name Immutable referral attributes
      @{*/
    uint256 hash;
    qint64 time;
    std::string address;
    std::string alias;

    /**@}*/

    /** Status: can change with block chain update */
    ReferralStatus status;

    /** Whether the referral parentAddress is a watch-only address */
    bool involvesWatchAddress;

    /** Update status from core wallet tx.
     */
    void updateStatus(referral::ReferralTx &rtx);

    /** Return whether a status update is needed.
     */
    bool statusUpdateNeeded() const { return true; } // TODO: implement this. Otherwise we always replae cache data

    // temporary
    QString displayString() const;
    QString statusString() const;
};

#endif // MERIT_QT_REFERRALRECORD_H
