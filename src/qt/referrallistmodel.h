// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_REFERRALLISTMODEL_H
#define MERIT_QT_REFERRALLISTMODEL_H

#include "referralrecord.h"

#include "validation.h"
#include "wallet/wallet.h"

#include <QAbstractListModel>

class PlatformStyle;
class ReferralRecord;
class ReferralListPriv;
class WalletModel;

class CWallet;

/** UI model for the referral list of a wallet.
 */
class ReferralListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ReferralListModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);
    ~ReferralListModel();

    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    // QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    enum RoleIndex {
        // includes alias if availabl
        AddressRole,
        DateRole,
        StatusRole
    };

private:
    const PlatformStyle *platformStyle;
    CWallet* wallet;
    WalletModel *walletModel;
    ReferralListPriv *priv;

public Q_SLOTS:
    /* New referral, or referral changed status */
    // void updateReferral(const QString &hash, int status, bool showReferral);

    friend class ReferralListPriv;
};

#endif // MERIT_QT_REFERRALLISTMODEL_H
