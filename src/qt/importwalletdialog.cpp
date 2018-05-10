#include <boost/algorithm/string.hpp>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QMessageBox>
#include <QTimer>

#include "config/merit-config.h" /* for USE_QRCODE */
#ifdef USE_QRCODE
    #include <qrencode.h>
#endif

#include "base58.h"
#include "guiconstants.h"
#include "qrutil.h"
#include "walletmodel.h"
#include "importwalletdialog.h"
#include "ui_importwalletdialog.h"
#include "crypto/mnemonic/mnemonic.h"

ImportWalletDialog::ImportWalletDialog(QWidget *parent, WalletModel *model) :
    QDialog(parent),
    walletModel(model),
    ui(new Ui::ImportWalletDialog)
{
    ui->setupUi(this);

    connect(ui->cancelButton, SIGNAL(clicked()), this, SLOT(OnCancelClicked()));
    connect(ui->importButton, SIGNAL(clicked()), this, SLOT(ImportWallet()));
    connect(ui->mnemonic, SIGNAL(textChanged()), this, SLOT(UpdateImportButton()));

    ui->importButton->setEnabled(false);
    ui->progressTitle->setVisible(false);
}

ImportWalletDialog::~ImportWalletDialog()
{
    delete ui;
}

void ImportWalletDialog::OnCancelClicked()
{
    reject();
}

void ImportWalletDialog::UpdateImportButton()
{
    auto mnemonic = ui->mnemonic->toPlainText().toStdString();
    boost::trim(mnemonic);
    const bool enabled = walletModel->IsAValidMnemonic(mnemonic);

    if(enabled) {
        ui->mnemonic->setStyleSheet("QPlainTextEdit { background-color: rgb(128, 255, 128) }");
    } else {
        ui->mnemonic->setStyleSheet("");
    }

    ui->importButton->setEnabled(enabled);
}

void ImportWalletDialog::ImportWallet()
{
    assert(walletModel);
    ui->cancelButton->setVisible(false);
    ui->importButton->setVisible(false);
    ui->progressTitle->setVisible(true);

    QTimer::singleShot(500, this, SLOT(DoImport()));
}

void ImportWalletDialog::DoImport()
{
    auto mnemonic = ui->mnemonic->toPlainText().toStdString();
    boost::trim(mnemonic);

    if(!walletModel->ImportMnemonicAsMaster(mnemonic)) {
        QMessageBox::critical(
                this,
                tr("Error importing wallet"),
                tr("Unable to import the wallet with the mnemonic given"));
        reject();
        return;
    }

    accept();
    return;
}

