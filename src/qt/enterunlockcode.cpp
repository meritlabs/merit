// Copyright (c) 2016-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "enterunlockcode.h"
#include "ui_enterunlockcode.h"
#include "importwalletdialog.h"

#include "guiutil.h"

#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QMessageBox>

const int ADDRESS_LENGTH = 34;

EnterUnlockCode::EnterUnlockCode(QWidget *parent) :
QWidget(parent),
ui(new Ui::EnterUnlockCode),
layerIsVisible(false),
userClosed(false)
{
    ui->setupUi(this);
    ui->aliasTextInput->setMaxLength(referral::MAX_ALIAS_LENGTH);
    connect(ui->unlockCodeTextInput, SIGNAL(textChanged(QString)), this, SLOT(unlockCodeChanged(QString)));
    connect(ui->aliasTextInput, SIGNAL(textChanged(QString)), this, SLOT(aliasChanged(QString)));
    connect(this, SIGNAL(CanSubmitChanged(bool)), ui->submitButton, SLOT(setEnabled(bool)));
    connect(ui->submitButton, SIGNAL(clicked()), this, SLOT(submit()));
    connect(ui->importButton, SIGNAL(clicked()), this, SLOT(importWallet()));
    if (parent) {
        parent->installEventFilter(this);
        raise();
    }

    ui->importButton->setEnabled(false);
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
    importWalletDialog = new ImportWalletDialog(this, walletModel);
    ui->importButton->setEnabled(true);
}

extern CTxDestination LookupDestination(const std::string& address);

void EnterUnlockCode::unlockCodeChanged(const QString &newText)
{
    auto parent = newText.toStdString();

    auto dest = LookupDestination(parent);

    CMeritAddress merit_address;
    merit_address.Set(dest);

    parentAddress = merit_address;

    addressValid = parentAddress.IsValid() 
        && walletModel->AddressBeaconed(parentAddress)
        && walletModel->AddressConfirmed(parentAddress);

    if (addressValid) {
        ui->unlockCodeTextInput->setStyleSheet("QLineEdit { background-color: rgb(128, 255, 128) }");
    } else {
        ui->unlockCodeTextInput->setStyleSheet("QLineEdit { background-color: rgb(255, 128, 128) }");
    }

    UpdateCanSubmit();
}

void EnterUnlockCode::aliasChanged(const QString &newText)
{
    auto alias = newText.toStdString();

    bool valid = referral::CheckReferralAliasSafe(alias);
    bool taken = !alias.empty() && walletModel->AliasExists(alias);

    aliasValid = valid && !taken;

    if(aliasValid) {
        ui->aliasTextInput->setStyleSheet("QLineEdit { background-color: rgb(128, 255, 128) }");
    } else {
        ui->aliasTextInput->setStyleSheet("QLineEdit { background-color: rgb(255, 128, 128) }");
    }

    UpdateCanSubmit();
}

void EnterUnlockCode::UpdateCanSubmit()
{
    canSubmit = addressValid && aliasValid;
    Q_EMIT CanSubmitChanged(canSubmit);
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

void EnterUnlockCode::importWallet()
{
    importWalletDialog->exec();

    if(importWalletDialog->result() == QDialog::Accepted) {
            Q_EMIT WalletReferred();
    }
}
