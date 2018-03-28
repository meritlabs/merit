// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_REFERRALLISTMODEL_H
#define MERIT_QT_REFERRALLISTMODEL_H

#include "referralrecord.h"

#include "validation.h"
#include "wallet/wallet.h"

#undef debug

#include <QAbstractListModel>
#include <QList>

class PlatformStyle;
class ReferralRecord;
class ReferralListPriv;
class WalletModel;

class CWallet;

class ReferralListPriv
{
public:
    ReferralListPriv(CWallet *_wallet);

    /* Query entire wallet anew from core.
     */
    void Refresh();
    int Size() const;

    ReferralRecord *Index(int idx);

    CWallet *wallet;

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QList<ReferralRecord> cachedWallet;
};

/** UI model for the referral list of a wallet.
 */
class ReferralListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ReferralListModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    // QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    enum RoleIndex {
        // includes alias if available
        AliasRole,
        DateRole,
        HashRole,
        AddressRole, // index 3 ; same as Qt::ToolTipRole
        StatusRole
    };

private:
    const PlatformStyle *platformStyle;
    CWallet* wallet;
    WalletModel *walletModel;
    std::unique_ptr<ReferralListPriv> priv;

public Q_SLOTS:

    void Refresh();
    /* New referral, or referral changed status */
    // void updateReferral(const QString &hash, int status, bool showReferral);

    friend class ReferralListPriv;
};

#endif // MERIT_QT_REFERRALLISTMODEL_H
