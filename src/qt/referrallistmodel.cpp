// Copyright (c) 2011-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrallistmodel.h"

#include <QDateTime>
#include <QDebug>
#include <QList>

class ReferralListPriv
{
public:
    ReferralListPriv(CWallet *_wallet) :
        wallet(_wallet)
    {
        refreshWallet();
    }

    CWallet *wallet;

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QList<ReferralRecord> cachedWallet;

    /* Query entire wallet anew from core.
     */
    void refreshWallet()
    {
        qDebug() << "ReferralListPriv::refreshWallet";
        cachedWallet.clear();
        {
            LOCK2(cs_main, wallet->cs_wallet);
            for(auto it = wallet->mapWalletRTx.begin(); it != wallet->mapWalletRTx.end(); ++it)
            {
                if(ReferralRecord::showReferral(it->second))
                    cachedWallet.append(ReferralRecord::decomposeReferral(wallet, it->second));
            }
        }
    }

    int size()
    {
        return cachedWallet.size();
    }

    ReferralRecord *index(int idx)
    {
        if(idx >= 0 && idx < cachedWallet.size())
        {
            ReferralRecord *rec = &cachedWallet[idx];

            // Get required locks upfront. This avoids the GUI from getting
            // stuck if the core is holding the locks for a longer time - for
            // example, during a wallet rescan.
            //
            // If a status update is needed (blocks came in since last check),
            //  update the status of this referral from the wallet. Otherwise,
            // simply re-use the cached status.
            TRY_LOCK(cs_main, lockMain);
            if(lockMain)
            {
                TRY_LOCK(wallet->cs_wallet, lockWallet);
                if(lockWallet && rec->statusUpdateNeeded())
                {
                    auto iter = wallet->mapWalletRTx.find(rec->hash);

                    if(iter != wallet->mapWalletRTx.end())
                    {
                        rec->updateStatus(iter->second);
                    }
                }
            }
            return rec;
        }
        return nullptr;
    }
};

ReferralListModel::ReferralListModel(const PlatformStyle *_platformStyle, CWallet *_wallet, WalletModel *parent):
    platformStyle(_platformStyle),
    wallet(_wallet),
    walletModel(parent),
    priv(new ReferralListPriv(_wallet))
{
}

ReferralListModel::~ReferralListModel()
{
    delete priv;
}

int ReferralListModel::rowCount(const QModelIndex &parent) const
{
    assert(priv);
    return priv->size();
}

QVariant ReferralListModel::data(const QModelIndex &index, int role) const
{
    assert(priv);
    if(!index.isValid() || index.row() >= priv->size())
        return QVariant();
    auto record = priv->index(index.row());
    if(record)
    {
        switch(role)
        {
        case AddressRole:
            return record->displayString();
        case StatusRole:
            return record->statusString();
        case DateRole:
            return QDateTime::fromSecsSinceEpoch(record->date);
        }
    }
    return QVariant();
}

// QVariant ReferralListModel::headerData(int section, Qt::Orientation orientation, int role) const;
// void ReferralListModel::updateReferral(const QString &hash, int status, bool showReferral);