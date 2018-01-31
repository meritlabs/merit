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

/** UI model for a referral.
 */
class ReferralRecord
{
public:
    enum Status {
        Pending,    // referral which needs an inviteTx to confirm
        Confirmed,  // referral which has been confirmed
        Declined,   // user refused to confirm this referral
    };

    ReferralRecord():
        hash{}, date{0}, address{}, alias{},status{Pending} {}

    ReferralRecord(uint256 _hash, qint64 _date, std::string _address, std::string _alias = ""):
        hash{_hash}, date{_date}, address{_address}, alias{_alias},status{Pending} {}

    /** @name Immutable referral attributes
      @{*/
    uint256 hash;
    qint64 date;
    std::string address;
    std::string alias;

    /**@}*/

    /** Status: can change with block chain update */
    Status status;

    /** Whether the referral parentAddress is a watch-only address */
    bool involvesWatchAddress;

    /** Update status from core wallet tx.
     */
    void UpdateStatus(const referral::ReferralRef&);

    /** Return whether a status update is needed.
     */
    bool StatusUpdateNeeded() const { return true; } // TODO: implement this. Otherwise we always replae cache data

    // temporary
    QString DisplayString() const;
    QString StatusString() const;
};

// addressType other than 1 means address is a script, no need to show those beacons here
bool ShowReferral(const referral::ReferralRef&);

namespace referral
{
    class RefMemPoolEntry;
}

ReferralRecord DecomposeReferral(const referral::ReferralTx &);
ReferralRecord DecomposeReferral(const referral::RefMemPoolEntry &);


#endif // MERIT_QT_REFERRALRECORD_H
