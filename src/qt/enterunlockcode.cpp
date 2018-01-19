// Copyright (c) 2016-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "enterunlockcode.h"
#include "ui_enterunlockcode.h"

#include "guiutil.h"

#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QMessageBox>

EnterUnlockCode::EnterUnlockCode(QWidget *parent) :
QWidget(parent),
ui(new Ui::EnterUnlockCode),
layerIsVisible(false),
userClosed(false)
{
    ui->setupUi(this);
    connect(ui->unlockCodeTextInput, SIGNAL(textChanged(QString)), this, SLOT(unlockCodeChanged(QString)));
    connect(this, SIGNAL(CanSubmitChanged(bool)), ui->submitButton, SLOT(setEnabled(bool)));
    connect(ui->submitButton, SIGNAL(clicked()), this, SLOT(submit()));
    connect(ui->unlockCodeTextInput, SIGNAL(returnPressed()), this, SLOT(submit()));
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

void EnterUnlockCode::unlockCodeChanged(const QString &newText)
{
    if(newText.length() < 34) {
        SetCanSubmit(false);
        return;
    }
    parentAddress.SetString(newText.toStdString());
    SetCanSubmit(parentAddress.IsValid());
}

void EnterUnlockCode::SetCanSubmit(bool _canSubmit)
{
    if(canSubmit != _canSubmit)
        Q_EMIT CanSubmitChanged(_canSubmit);

    canSubmit = _canSubmit;
}

void EnterUnlockCode::submit()
{
    if(!canSubmit)
        return;

    auto parentAddressUint160 = parentAddress.GetUint160();
    if(parentAddressUint160) {
        try {
            auto alias = ui->aliasTextInput->text().toStdString();

            referral::ReferralRef referral =
                walletModel->Unlock(*parentAddressUint160, alias);

            Q_EMIT WalletReferred();
        } catch (const std::runtime_error &err) {
            QMessageBox::warning(
                this, 
                tr("Sorry, there was a problem."), 
                tr(err.what()));
        }
    }
}
