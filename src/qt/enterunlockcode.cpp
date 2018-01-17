// Copyright (c) 2016-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"

#include "enterunlockcode.h"
#include "ui_enterunlockcode.h"

#include "guiutil.h"

#include <QResizeEvent>
#include <QPropertyAnimation>

EnterUnlockCode::EnterUnlockCode(QWidget *parent) :
QWidget(parent),
ui(new Ui::EnterUnlockCode),
layerIsVisible(false),
userClosed(false)
{
    ui->setupUi(this);
    connect(ui->submitButton, SIGNAL(clicked()), this, SLOT(submitButtonClicked()));
    if (parent) {
        parent->installEventFilter(this);
        raise();
    }

    setVisible(false);
}

EnterUnlockCode::~EnterUnlockCode()
{
    delete ui;
}

bool EnterUnlockCode::eventFilter(QObject * obj, QEvent * ev) {
    if (obj == parent()) {
        if (ev->type() == QEvent::Resize) {
            QResizeEvent * rev = static_cast<QResizeEvent*>(ev);
            resize(rev->size());
            if (!layerIsVisible)
                setGeometry(0, height(), width(), height());

        }
        else if (ev->type() == QEvent::ChildAdded) {
            raise();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool EnterUnlockCode::event(QEvent* ev) {
    if (ev->type() == QEvent::ParentAboutToChange) {
        if (parent()) parent()->removeEventFilter(this);
    }
    else if (ev->type() == QEvent::ParentChange) {
        if (parent()) {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}

void EnterUnlockCode::toggleVisibility()
{
    showHide(layerIsVisible, true);
    if (!layerIsVisible)
        userClosed = true;
}

void EnterUnlockCode::showHide(bool hide, bool userRequested)
{
    if ( (layerIsVisible && !hide) || (!layerIsVisible && hide) || (!hide && userClosed && !userRequested))
        return;

    if (!isVisible() && !hide)
        setVisible(true);

    setGeometry(0, hide ? 0 : height(), width(), height());

    QPropertyAnimation* animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, hide ? 0 : this->height()));
    animation->setEndValue(QPoint(0, hide ? this->height() : 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = !hide;
}

void EnterUnlockCode::setModel(WalletModel *model)
{
    this->walletModel = model;
}

void EnterUnlockCode::closeClicked()
{
    showHide(true);
    userClosed = true;
}

void EnterUnlockCode::submitButtonClicked()
{
    CMeritAddress parentAddress
    {
        ui->unlockCodeTextInput->toPlainText().toStdString()
    };

    if (!parentAddress.IsValid()) {
        // TODO: handle it
    }

    auto parentAddressUint160 = parentAddress.GetUint160();
    if(parentAddressUint160)
        referral::ReferralRef referral = walletModel->Unlock(*parentAddressUint160);
}