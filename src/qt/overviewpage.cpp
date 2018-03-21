// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "meritunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "referrallistmodel.h"
#include "walletmodel.h"

#include <QAbstractItemDelegate>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QTimer>

namespace
{
    const int DECORATION_SIZE = 54;
    const int NUM_ITEMS = 10;
    const int SPREAD_MARGIN_W = 50;
    const int SPREAD_MARGIN_H = 10;
}

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(MeritUnits::MRT),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QRect mainRect = option.rect;
        int xpad = 8;
        int ypad = 10;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xpad, mainRect.top()+ypad, mainRect.width() - 2*xpad, halfheight);
        QRect timestampRect(mainRect.left() + xpad, mainRect.top()+ypad+halfheight, mainRect.width() - xpad, halfheight);
        QLine line(mainRect.left() + xpad, mainRect.bottom(), mainRect.right() - xpad, mainRect.bottom());

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        qint64 invitesNumber = index.data(TransactionTableModel::InviteRole).toLongLong();

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        painter->setPen(COLOR_BAREADDRESS);
        painter->drawText(timestampRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        QFont font;
        font.setBold(true);
        font.setWeight(QFont::Bold);
        painter->setFont(font);

        QString amountText;

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
            amountText = QString("Sent: ");
            amount = -amount;
        }
        else
        {
            foreground = COLOR_LIGHTBLUE;
            int txType = index.data(TransactionTableModel::TypeRole).toInt();

            switch(txType) {
                case TransactionRecord::Generated:
                    amountText = QString("Mining Reward: ");
                    break;
                case TransactionRecord::GeneratedInvite:
                    amountText = QString("Invite: ");
                    break;
                case TransactionRecord::AmbassadorReward:
                    amountText = QString("Ambassador Reward: ");
                    break;
            }
        }

        if (index.data(TransactionTableModel::IsInviteRole).toBool()) {
            QString plurality = invitesNumber > 1 ? QString("s") : QString();
            amountText += QString::number(invitesNumber) + QString(" Invite") + plurality;
        } else {
            amountText += MeritUnits::formatWithUnit(unit, amount, false, MeritUnits::separatorAlways);
        }

        painter->setPen(foreground);
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, amountText);

        if(!confirmed)
        {
            painter->setPen(COLOR_UNCONFIRMED);
            painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, QStringLiteral("(unconfirmed)"));
        }

        painter->setPen(Qt::lightGray);
        painter->drawLine(line);

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};

class ReferralViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit ReferralViewDelegate(const CAmount& _invite_balance, const bool& _is_daedalus, const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate{parent}, unit{MeritUnits::MRT},
        platformStyle{_platformStyle}, invite_balance{_invite_balance}, is_daedalus{_is_daedalus}
    {}
    const int XPAD = 8;
    const int YPAD = 10;
    const int INVITE_BUTTON_WIDTH = 80;

    inline QRect AddressRect(const QRect& mainRect, int height) const
    {
        return QRect(mainRect.left() + XPAD, mainRect.top()+YPAD, mainRect.width() - 2*XPAD, height);
    }

    inline QRect InviteRect(const QRect& mainRect, int height) const
    {
        QRect addressRect = AddressRect(mainRect, height);
        return QRect(addressRect.right() - INVITE_BUTTON_WIDTH, mainRect.top()+YPAD, INVITE_BUTTON_WIDTH, height);
    }

    inline QRect DeclineRect(const QRect& mainRect, int height) const
    {
        QRect addressRect = AddressRect(mainRect, height);
        return QRect(addressRect.right() - 2*INVITE_BUTTON_WIDTH - XPAD, mainRect.top()+YPAD, INVITE_BUTTON_WIDTH, height);
    }

    inline void DrawButton(QPainter *painter, const QRect& rect, const QString& text, const QColor& color) const
    {
        auto button_rect = painter->boundingRect(rect, text);
        button_rect.setLeft(button_rect.left() - 10);
        button_rect.setRight(button_rect.right() + 10);
        button_rect.setTop(button_rect.top() - 2);
        button_rect.setBottom(button_rect.bottom() + 2);

        QPen pen;
        pen.setColor(color);
        painter->setPen(pen);

        QPainterPath path;
        path.addRoundedRect(button_rect, 10, 10);
        painter->fillPath(path, color);
        painter->drawPath(path);

        painter->setPen(Qt::white);
        painter->drawText(button_rect, Qt::AlignCenter|Qt::AlignVCenter, text);
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        QRect mainRect = option.rect;
        int halfheight = (mainRect.height() - 2*YPAD)/2;

        QRect addressRect = AddressRect(mainRect, halfheight);
        QRect timestampRect(mainRect.left() + XPAD, mainRect.top()+YPAD+halfheight, mainRect.width() - XPAD, halfheight);
        QLine line(mainRect.left() + XPAD, mainRect.bottom(), mainRect.right() - XPAD, mainRect.bottom());

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);

        QDateTime date = index.data(ReferralListModel::DateRole).toDateTime();
        QString addressString = index.data(ReferralListModel::AddressRole).toString();
        QString aliasString = index.data(ReferralListModel::AliasRole).toString();
        QString displayString = aliasString.isEmpty() ? QString("Anonymous User") :
            QString("@") + aliasString;

        painter->setPen(COLOR_BAREADDRESS);
        painter->drawText(timestampRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        QFont font;
        font.setBold(true);
        font.setWeight(QFont::Bold);
        painter->setFont(font);
        painter->setPen(COLOR_NEGATIVE);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, displayString);

        QString statusString = index.data(ReferralListModel::StatusRole).toString();

        if(statusString == "Pending" && is_daedalus) {
            QRect inviteRect = InviteRect(mainRect, halfheight);
            QRect declineRect = DeclineRect(mainRect, halfheight);

            QColor merit_blue = invite_balance > 0 ? QColor{0, 176, 220} : QColor{128, 128, 128};
            DrawButton(painter, inviteRect, tr("Send Invite"), merit_blue);
            DrawButton(painter, declineRect, tr("Decline"), Qt::gray);
        }


        painter->setPen(Qt::lightGray);
        painter->drawLine(line);

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    inline bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
    {
        QString statusString = index.data(ReferralListModel::StatusRole).toString();
        if(statusString != "Pending")
            return true;

        if (event->type() != QEvent::MouseButtonRelease) {
            return true;
        }

        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        auto inviteBox = InviteRect(option.rect, (option.rect.height() - 2*YPAD)/2);
        auto declineBox = DeclineRect(option.rect, (option.rect.height() - 2*YPAD)/2);

        if(inviteBox.contains(mouseEvent->pos())) {
            Q_EMIT invite(index);
            return true;
        }
        if(declineBox.contains(mouseEvent->pos())) {
            Q_EMIT decline(index);
            return true;
        }
        return false;
    }

    int unit;
    const PlatformStyle *platformStyle;
    const CAmount& invite_balance;
    const bool& is_daedalus;

Q_SIGNALS:
    void invite(QModelIndex);
    void decline(QModelIndex);

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    referraldelegate(new ReferralViewDelegate(currentInviteBalance, currentIsDaedalus, platformStyle, this)),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    SetShadows();

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);
    ui->networkAlertLabel->setIcon(icon);
    ui->requestsAlertLabel->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setMinimumHeight(DECORATION_SIZE + 2);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->inviteNotice->hide();

    // Unlock Requests
    ui->listPendingRequests->setItemDelegate(referraldelegate);
    ui->listPendingRequests->setMinimumHeight(DECORATION_SIZE + 2);
    ui->listPendingRequests->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listApprovedRequests->setItemDelegate(referraldelegate);
    ui->listApprovedRequests->setMinimumHeight(DECORATION_SIZE + 2);
    ui->listApprovedRequests->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    connect(referraldelegate, SIGNAL(invite(QModelIndex)), this, SLOT(handleInviteClicked(QModelIndex)));
    connect(referraldelegate, SIGNAL(decline(QModelIndex)), this, SLOT(handleDeclineClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->networkAlertLabel, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->requestsAlertLabel, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));

    spread_pixmap = new QPixmap{":/icons/spread"};
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(txFilter)
        Q_EMIT transactionClicked(txFilter->mapToSource(index));
}

void OverviewPage::handleInviteClicked(const QModelIndex &index)
{
    if(!walletModel) {
        return;
    }

    if(currentInviteBalance == 0) {
        QMessageBox::warning(this, tr("No Invites Available"), tr("You do not have any invites left"));
        return;
    }

    QString statusString = index.data(ReferralListModel::StatusRole).toString();
    if(statusString != "Pending") {
        return;
    }

    QString addressString = index.data(ReferralListModel::AddressRole).toString();
    QString aliasString = index.data(ReferralListModel::AliasRole).toString();

    QString title = aliasString.isEmpty() ?
        tr("Invite") + " " + addressString :
        tr("Invite") + " " + aliasString;

    QString text = aliasString.isEmpty() ?
        tr("Do you want to invite") + " " + addressString + "?":
        tr("Do you want to invite") + " @" + aliasString + " " + tr("with the address") + " " + addressString + "?";
    
    QMessageBox msgBox{QMessageBox::Question,
                        title, text,
                        QMessageBox::Yes | QMessageBox::No,
                        this};
    msgBox.setStyleSheet(QString("QMessageBox { background-color: white; }"));
    auto ret = msgBox.exec();
    if(ret != QMessageBox::Yes) {
        return;
    }

    auto success = walletModel->SendInviteTo(addressString.toStdString());
    if(!success) {
        QString title = aliasString.isEmpty() ?
            tr("Error Inviting") + " " + addressString :
            tr("Error Inviting") + " " + aliasString;

        QString text = aliasString.isEmpty() ?
            tr("There was an error inviting") + " " + addressString:
            tr("There was an error inviting") + " " + aliasString + " " + tr("with the address") + " " + addressString;

        QMessageBox::critical(this, title, text);
    }
}

void OverviewPage::handleDeclineClicked(const QModelIndex &index)
{
    if(!walletModel) {
        return;
    }

    QString statusString = index.data(ReferralListModel::StatusRole).toString();
    if(statusString != "Pending") {
        return;
    }

    QString addressString = index.data(ReferralListModel::AddressRole).toString();
    QString aliasString = index.data(ReferralListModel::AliasRole).toString();

    QString title = aliasString.isEmpty() ?
        tr("Decline Invite") + " " + addressString :
        tr("Decline Invite") + " " + aliasString;

    QString text = aliasString.isEmpty() ?
        tr("Do you want to decline an invite request from") + " " + addressString + "?":
        tr("Do you want to decline an invite request from") + " @" + aliasString + " " + tr("with the address") + " " + addressString + "?";
    
    QMessageBox msgBox{QMessageBox::Question,
                        title, text,
                        QMessageBox::Yes | QMessageBox::No,
                        this};
    msgBox.setStyleSheet(QString("QMessageBox { background-color: white; }"));
    auto ret = msgBox.exec();
    if(ret != QMessageBox::Yes) {
        return;
    }

    QString hashString = index.data(ReferralListModel::HashRole).toString();
    auto success = walletModel->DeclineInviteTo(hashString.toStdString());
    if(!success) {
        QString title = aliasString.isEmpty() ?
            tr("Error Declining Invite") + " " + addressString :
            tr("Error Declining Invite") + " " + aliasString;

        QString text = aliasString.isEmpty() ?
            tr("There was an error declining the invite request from") + " " + addressString:
            tr("There was an error declining the invite request from") + " " + aliasString + " " + tr("with the address") + " " + addressString;

        QMessageBox::critical(this, title, text);
    }

    UpdateNetworkView();
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

QString OverviewPage::FormatInviteBalance(CAmount invites)
{
    QString color = invites == 0 ? "#aa0000" : "#00aa00";
    QString inviteOrInvites = invites == 1 ? tr("Invite") : tr("Invites");

    return QString("<html><head/><body><p><span style=\" font-size:12pt; font-weight:600; color:")
        + color
        + QString(";\">")
        + QString::number(invites)
        + "</span><span style=\" font-size: 12pt; font-weight:600;\"> "
        + tr("Available")
        + " "
        + inviteOrInvites
        + "</span></p></body></html>";
}

void OverviewPage::setBalance(
        CAmount balance,
        CAmount unconfirmedBalance,
        CAmount immatureBalance,
        CAmount watchOnlyBalance,
        CAmount watchUnconfBalance,
        CAmount watchImmatureBalance,
        CAmount inviteBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentInviteBalance = inviteBalance;

    ui->labelBalance->setText(MeritUnits::formatWithUnit(unit, balance, false, MeritUnits::separatorAlways));
    ui->labelUnconfirmed->setText(MeritUnits::formatWithUnit(unit, unconfirmedBalance, false, MeritUnits::separatorAlways));
    ui->labelImmature->setText(MeritUnits::formatWithUnit(unit, immatureBalance, false, MeritUnits::separatorAlways));
    ui->labelTotal->setText(MeritUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance, false, MeritUnits::separatorAlways));
    ui->labelWatchAvailable->setText(MeritUnits::formatWithUnit(unit, watchOnlyBalance, false, MeritUnits::separatorAlways));
    ui->labelWatchPending->setText(MeritUnits::formatWithUnit(unit, watchUnconfBalance, false, MeritUnits::separatorAlways));
    ui->labelWatchImmature->setText(MeritUnits::formatWithUnit(unit, watchImmatureBalance, false, MeritUnits::separatorAlways));
    ui->labelWatchTotal->setText(MeritUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, MeritUnits::separatorAlways));
    ui->inviteBalance->setText(FormatInviteBalance(inviteBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
    ui->inviteBalance->setVisible(walletModel->Daedalus());

    UpdateInvitationStatus();
}

void OverviewPage::setYourCommunity(
        const QString &alias,
        const QString &address)
{
    if(alias.length() > 0)
    {
        ui->aliasTitleLabel->setHidden(false);
        ui->aliasFieldLabel->setHidden(false);
        ui->aliasFieldLabel->setText(alias);
    }
    else
    {
        ui->aliasTitleLabel->setHidden(true);
        ui->aliasFieldLabel->setHidden(true);
    }

    ui->unlockCodeFieldLabel->setText(address);
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        connect(model, SIGNAL(mempoolSizeChanged(long,size_t)), this, SLOT(MempoolSizeChanged(long, size_t)));
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime, double,bool)), this, SLOT(UpdateNetworkView()));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        txFilter.reset(new TransactionFilterProxy());
        txFilter->setSourceModel(model->getTransactionTableModel());
        txFilter->setLimit(NUM_ITEMS);
        txFilter->setDynamicSortFilter(true);
        txFilter->setSortRole(Qt::EditRole);
        txFilter->setShowInactive(false);
        txFilter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        pendingRequestsFilter.reset(new QSortFilterProxyModel(this));
        pendingRequestsFilter->setSourceModel(model->getReferralListModel());
        pendingRequestsFilter->setFilterRole(ReferralListModel::StatusRole);
        pendingRequestsFilter->setFilterFixedString(QString("Pending"));

        approvedRequestsFilter.reset(new QSortFilterProxyModel(this));
        approvedRequestsFilter->setSourceModel(model->getReferralListModel());
        approvedRequestsFilter->setFilterRole(ReferralListModel::StatusRole);
        approvedRequestsFilter->setFilterFixedString(QString("Confirmed"));

        ui->listTransactions->setModel(txFilter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        ui->listPendingRequests->setModel(pendingRequestsFilter.get());
        ui->listApprovedRequests->setModel(approvedRequestsFilter.get());

        // show up to 5 pending invite requests before having to scroll to view more.
        ui->listPendingRequests->setMinimumHeight(
            std::min(5, pendingRequestsFilter->rowCount()) * (DECORATION_SIZE + 2));
        ui->listPendingRequests->adjustSize();

        is_confirmed = walletModel->IsConfirmed();
        UpdateInvitationStatus();
        UpdateInviteRequestView();

        // Keep up to date with wallet
        setBalance(
                model->getBalance(),
                model->getUnconfirmedBalance(),
                model->getImmatureBalance(),
                model->getWatchBalance(),
                model->getWatchUnconfirmedBalance(),
                model->getWatchImmatureBalance(),
                model->getBalance(nullptr, true));
        
        setYourCommunity(
                model->GetAlias(),
                model->GetUnlockCode());

        connect(
                model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)),
                this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model, SIGNAL(transactionUpdated()), this, SLOT(UpdateInvitationStatus()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("MRT")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(!(walletModel && walletModel->getOptionsModel())) {
        return;
    }

    if(currentBalance != -1)
        setBalance(
                currentBalance,
                currentUnconfirmedBalance,
                currentImmatureBalance,
                currentWatchOnlyBalance,
                currentWatchUnconfBalance,
                currentWatchImmatureBalance,
                currentInviteBalance);

    // Update txdelegate->unit with the current unit
    txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

    ui->listTransactions->update();
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
    ui->networkAlertLabel->setVisible(fShow);
    ui->requestsAlertLabel->setVisible(fShow);
}

void OverviewPage::HideInviteNotice()
{
    QPropertyAnimation* animation = new QPropertyAnimation(ui->inviteNotice, "size");
    animation->setDuration(300);
    animation->setStartValue(QSize(ui->inviteNotice->width(), ui->inviteNotice->height()));
    animation->setEndValue(QSize(ui->inviteNotice->width(), 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    QTimer::singleShot(400, ui->inviteNotice, SLOT(hide()));
}

void OverviewPage::UpdateInvitationStatus()
{
    assert(ui);
    if(!walletModel) return;

    currentIsDaedalus = walletModel->Daedalus();

    if(is_confirmed || !walletModel->Daedalus()) {
        ui->inviteNotice->hide();
        return;
    }

    bool confirmed = walletModel->IsConfirmed();
    if(!confirmed) {
        ui->inviteNotice->show();
    } else {
        ui->inviteNotice->setStyleSheet("QLabel {background-color: rgb(128, 255, 128)}");
        ui->inviteNotice->setText("<html><head/><body><p align=\"center\"><span style=\" font-size:12pt; font-weight:600;\">You Have Been Invited to Merit!</span></p></body></html>");
        QTimer::singleShot(3000, this, SLOT(HideInviteNotice()));
    }
    is_confirmed = confirmed;
}

void OverviewPage::UpdateInviteRequestView() 
{
    assert(ui);
    assert(spread_pixmap);
    if(!walletModel) return;

    assert(pendingRequestsFilter);
    assert(approvedRequestsFilter);

    const bool has_requests = pendingRequestsFilter->rowCount() > 0;
    const bool has_approved = approvedRequestsFilter->rowCount() > 0;


    if (has_requests) {
        ui->spreadTheWord->hide();
        ui->noPendingInvitesLabel->hide();
        ui->listPendingRequests->setMinimumHeight(
                std::min(5, pendingRequestsFilter->rowCount()) * (DECORATION_SIZE + 2));
        ui->listPendingRequests->show();
    } else { 
        ui->listPendingRequests->hide();
        ui->noPendingInvitesLabel->show();
        ui->spreadTheWord->setHidden(true);
        const auto s = ui->spreadTheWordIcon->size();

        const auto ps =
            static_cast<QWidget*>(ui->spreadTheWordIcon->parent())->size();

        const auto w = std::max(50, ps.width() - SPREAD_MARGIN_W);
        const auto h = s.height() - SPREAD_MARGIN_H;

        auto scaled_pixmap = spread_pixmap->scaled(
                w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        ui->spreadTheWordIcon->setPixmap(scaled_pixmap);
        ui->spreadTheWordIcon->setMinimumHeight(h);
        ui->spreadTheWord->adjustSize();
        ui->spreadTheWord->setHidden(false);
    }

    if(!has_approved) {
        ui->approvedRequestsLabel->hide();
        ui->listApprovedRequests->hide();
    } else {
        ui->approvedRequestsLabel->show();
        ui->listApprovedRequests->show();
    }
}

void OverviewPage::MempoolSizeChanged(long size, size_t bytes)
{
    if(size == mempool_size && bytes == mempool_bytes) {
        return;
    }

    UpdateNetworkView();
    UpdateInviteRequestView();

    mempool_size = size;
    mempool_bytes = bytes;
}

void OverviewPage::resizeEvent(QResizeEvent* e)
{
    UpdateInviteRequestView();
    QWidget::resizeEvent(e);
}

void OverviewPage::UpdateNetworkView()
{
    if(!walletModel) {
        return;
    }

    setYourCommunity(
            walletModel->GetAlias(),
            walletModel->GetUnlockCode());

    auto ref_model = walletModel->getReferralListModel();
    if(ref_model) {
        ref_model->Refresh();
    }
}

QGraphicsDropShadowEffect* MakeFrameShadowEffect()
{
    auto effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(20);
    effect->setXOffset(0);
    effect->setYOffset(0);
    effect->setColor(Qt::lightGray);
    return effect;
}

void OverviewPage::SetShadows()
{
    ui->balanceFrame->setGraphicsEffect(MakeFrameShadowEffect());
    ui->transactionsFrame->setGraphicsEffect(MakeFrameShadowEffect());
    ui->communityFrame->setGraphicsEffect(MakeFrameShadowEffect());
    ui->unlockRequestFrame->setGraphicsEffect(MakeFrameShadowEffect());
}
