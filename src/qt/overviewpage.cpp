// Copyright (c) 2011-2017 The Merit Foundation developers
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
        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else
        {
            foreground = COLOR_LIGHTBLUE;
        }
        QString amountText = MeritUnits::formatWithUnit(unit, amount, true, MeritUnits::separatorAlways);
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
    explicit ReferralViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
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
        QRect addressRect(mainRect.left() + xpad, mainRect.top()+ypad, mainRect.width() - 2*xpad, halfheight);
        QRect timestampRect(mainRect.left() + xpad, mainRect.top()+ypad+halfheight, mainRect.width() - xpad, halfheight);
        QLine line(mainRect.left() + xpad, mainRect.bottom(), mainRect.right() - xpad, mainRect.bottom());

        QDateTime date = index.data(ReferralListModel::DateRole).toDateTime();
        QString addressString = index.data(ReferralListModel::AddressRole).toString();
        QString statusString = index.data(ReferralListModel::StatusRole).toString();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;

        painter->setPen(COLOR_BAREADDRESS);
        painter->drawText(timestampRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        QFont font;
        font.setBold(true);
        font.setWeight(QFont::Bold);
        painter->setFont(font);
        painter->setPen(COLOR_NEGATIVE);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, addressString);

        painter->drawText(addressRect, Qt::AlignRight|Qt::AlignVCenter, statusString);

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
    referraldelegate(new ReferralViewDelegate(platformStyle, this)),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    SetShadows();

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->inviteNotice->hide();

    // Unlock Requests
    ui->listRequests->setItemDelegate(referraldelegate);
    ui->listRequests->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listRequests->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
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
        + tr("Avaliable") 
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
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        ui->listRequests->setModel(model->getReferralListModel());

        is_confirmed = walletModel->IsConfirmed();
        UpdateInvitationStatus();

        // Keep up to date with wallet
        setBalance(
                model->getBalance(),
                model->getUnconfirmedBalance(),
                model->getImmatureBalance(),
                model->getWatchBalance(),
                model->getWatchUnconfirmedBalance(),
                model->getWatchImmatureBalance(),
                model->getBalance(nullptr, true));

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
    UpdateInvitationStatus();
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
    if(is_confirmed || !walletModel->Daedalus()) {
        ui->inviteNotice->hide();
        return;
    }

    bool confirmed = walletModel->IsConfirmed();
    if(!confirmed) {
        ui->inviteNotice->show();
    } else {
        ui->inviteNotice->setStyleSheet("QLabel {background-color: rgb(128, 255, 128)}");
        ui->inviteNotice->setText("<html><head/><body><p align=\"center\"><span style=\" font-size:12pt; font-weight:600;\">Your invitation request is accepted!</span></p></body></html>");
        QTimer::singleShot(3000, this, SLOT(HideInviteNotice()));
    }
    is_confirmed = confirmed;
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
    ui->networkFrame->setGraphicsEffect(MakeFrameShadowEffect());
}
