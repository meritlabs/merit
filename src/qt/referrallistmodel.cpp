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
    Refresh();
}

using AddressSet = std::set<uint160>;

bool DisplayReferral(
        const AddressSet& addresses,
        const CWallet* wallet,
        const referral::ReferralRef ref)
{
    assert(wallet);
    assert(ref);

    const auto& addr = ref->GetAddress();

    return ShowReferral(ref) 
        && addresses.count(addr) == 0
        && wallet->IsMine(*ref) 
        && !wallet->IsMe(*ref) 
        && CheckAddressBeaconed(addr);
} 

void ReferralListPriv::Refresh()
{
    qDebug() << "ReferralListPriv::refreshWallet";
    cachedWallet.clear();
    std::set<uint160> addresses;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        for (const auto& entry : mempoolReferral.mapRTx) {
            const auto ref = entry.GetSharedEntryValue();
            if(DisplayReferral(addresses, wallet, ref)) {
                auto rec = DecomposeReferral(entry);
                rec.UpdateStatus(ref);
                cachedWallet.append(rec);
                addresses.insert(ref->GetAddress());
            }
        }

        for (const auto& entry : wallet->mapWalletRTx) {
            const auto ref = entry.second.GetReferral();
            if(DisplayReferral(addresses, wallet, ref)) {
                auto rec = DecomposeReferral(entry.second);
                rec.UpdateStatus(ref);
                cachedWallet.append(rec);
                addresses.insert(ref->GetAddress());
            }
        }
    }

    std::sort(cachedWallet.begin(), cachedWallet.end(),
            [](const ReferralRecord& a, const ReferralRecord& b) {
                if(a.status == b.status) {
                    return a.date > b.date;
                }
                return a.status < b.status;
            });
}

int ReferralListPriv::Size() const
{
    return cachedWallet.size();
}

ReferralRecord *ReferralListPriv::Index(int idx)
{
    if (idx < 0 || idx >= cachedWallet.size()) {
        return nullptr;
    }

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
            return QString::fromStdString(record->address);
        case AliasRole:
            return QString::fromStdString(record->alias);
        case StatusRole:
            return record->StatusString();
        case DateRole:
            return QDateTime::fromTime_t(static_cast<uint>(record->date));
        }
    }
    return QVariant();
}

void ReferralListModel::Refresh()
{
    assert(priv);
    priv->Refresh();

    Q_EMIT dataChanged(index(0, 0), index(priv->Size()-1, 0));
}

// QVariant ReferralListModel::headerData(int section, Qt::Orientation orientation, int role) const;
// void ReferralListModel::updateReferral(const QString &hash, int status, bool showReferral);
