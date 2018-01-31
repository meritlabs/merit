// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrallistmodel.h"
#include "refmempool.h"

#include <QDateTime>
#include <QDebug>
#include <QList>


extern referral::ReferralTxMemPool mempoolReferral;

ReferralListPriv::ReferralListPriv(CWallet *_wallet) : wallet{_wallet}
{
    RefreshWallet();
}

void ReferralListPriv::RefreshWallet()
{
    qDebug() << "ReferralListPriv::refreshWallet";
    cachedWallet.clear();
    {
        LOCK2(cs_main, wallet->cs_wallet);
        for (const auto& entry : mempoolReferral.mapRTx) {
            const auto ref = entry.GetSharedEntryValue();
            assert(ref);
            if (ShowReferral(ref) && wallet->IsMine(*ref) && !wallet->IsMe(*ref)) {
                cachedWallet.append(DecomposeReferral(entry));
            }
        }

        for (const auto& entry : wallet->mapWalletRTx) {
            const auto ref = entry.second.GetReferral();
            if (ShowReferral(ref) && !wallet->IsMe(*ref)) {
                cachedWallet.append(DecomposeReferral(entry.second));
            }
        }
    }
}

int ReferralListPriv::Size() const
{
    return cachedWallet.size();
}

ReferralRecord *ReferralListPriv::Index(int idx)
{
    if (idx >= 0 && idx < cachedWallet.size()) {
        auto rec = &cachedWallet[idx];

        // Get required locks upfront. This avoids the GUI from getting
        // stuck if the core is holding the locks for a longer time - for
        // example, during a wallet rescan.
        //
        // If a status update is needed (blocks came in since last check),
        //  update the status of this referral from the wallet. Otherwise,
        // simply re-use the cached status.
        TRY_LOCK(cs_main, lockMain);
        if (lockMain)
        {
            TRY_LOCK(wallet->cs_wallet, lockWallet);
            if (lockWallet && rec->StatusUpdateNeeded()) {
                auto iter = wallet->mapWalletRTx.find(rec->hash);

                if (iter != wallet->mapWalletRTx.end()) {
                    rec->UpdateStatus(iter->second.GetReferral());
                }
            }
        }
        return rec;
    }
    return nullptr;
}

ReferralListModel::ReferralListModel(const PlatformStyle *_platformStyle, CWallet *_wallet, WalletModel *parent):
    platformStyle{_platformStyle},
    wallet{_wallet},
    walletModel{parent},
    priv{new ReferralListPriv{_wallet}}
{
}

int ReferralListModel::rowCount(const QModelIndex &parent) const
{
    assert(priv);
    return priv->Size();
}

QVariant ReferralListModel::data(const QModelIndex &index, int role) const
{
    assert(priv);
    if (!index.isValid() || index.row() >= priv->Size()) {
        return QVariant();
    }

    auto record = priv->Index(index.row());

    if (record) {
        switch (role) {
        case AddressRole:
            return record->DisplayString();
        case StatusRole:
            return record->StatusString();
        case DateRole:
            return QDateTime::fromSecsSinceEpoch(record->date);
        }
    }
    return QVariant();
}

// QVariant ReferralListModel::headerData(int section, Qt::Orientation orientation, int role) const;
// void ReferralListModel::updateReferral(const QString &hash, int status, bool showReferral);
