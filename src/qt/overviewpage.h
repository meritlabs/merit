// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_OVERVIEWPAGE_H
#define MERIT_QT_OVERVIEWPAGE_H

#include "amount.h"

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class QSortFilterProxyModel;
class ReferralViewDelegate;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);

public Q_SLOTS:
    void setBalance(
            CAmount balance,
            CAmount unconfirmedBalance,
            CAmount immatureBalance,
            CAmount watchOnlyBalance,
            CAmount watchUnconfBalance,
            CAmount watchImmatureBalance,
            CAmount inviteBalance);
    void setYourCommunity(
            const QString &alias,
            const QString &address);
    void UpdateInvitationStatus();
    void UpdateInviteRequestView();
    void UpdateNetworkView();
    void HideInviteNotice();
    void MempoolSizeChanged(long size, size_t bytes);
    void resizeEvent(QResizeEvent*);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;
    CAmount currentInviteBalance;
    bool currentIsDaedalus;

    ReferralViewDelegate *referraldelegate;
    TxViewDelegate *txdelegate;
    std::unique_ptr<TransactionFilterProxy> txFilter;
    std::unique_ptr<QSortFilterProxyModel> pendingRequestsFilter;
    std::unique_ptr<QSortFilterProxyModel> approvedRequestsFilter;
    bool is_confirmed = false;

    long mempool_size = 0;
    size_t mempool_bytes = 0;

    QPixmap* spread_pixmap;

private:
    QString FormatInviteBalance(CAmount invites);
    void SetShadows();


private Q_SLOTS:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void handleInviteClicked(const QModelIndex &index);
    void handleIgnoreClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
};

#endif // MERIT_QT_OVERVIEWPAGE_H
