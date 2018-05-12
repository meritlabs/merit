#include <boost/algorithm/string.hpp>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QMessageBox>
#include <QTimer>
#include <chrono>

#include "config/merit-config.h" /* for USE_QRCODE */
#ifdef USE_QRCODE
    #include <qrencode.h>
#endif

#include "base58.h"
#include "guiconstants.h"
#include "qrutil.h"
#include "walletmodel.h"
#include "importwalletdialog.h"
#include "ui_interface.h"
#include "ui_importwalletdialog.h"
#include "crypto/mnemonic/mnemonic.h"

ImportWalletDialog::ImportWalletDialog(QWidget *parent, WalletModel *wmodel) :
    QDialog(parent),
    model(wmodel),
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
    const bool enabled = model->IsAValidMnemonic(mnemonic);

    if(enabled) {
        ui->mnemonic->setStyleSheet("QPlainTextEdit { background-color: rgb(128, 255, 128) }");
    } else {
        ui->mnemonic->setStyleSheet("");
    }

    ui->importButton->setEnabled(enabled);
}

void ImportWalletDialog::ImportWallet()
{
    assert(model);
    ui->cancelButton->setVisible(false);
    ui->importButton->setVisible(false);
    ui->progressTitle->setVisible(true);

    QTimer::singleShot(500, this, SLOT(DoImport()));
}

void ImportWalletDialog::DoImport()
{
    auto mnemonic = ui->mnemonic->toPlainText().toStdString();
    boost::trim(mnemonic);

    import_result = std::async(std::launch::async, [this,mnemonic]() {
                return model->ImportMnemonicAsMaster(mnemonic);
            });

    QTimer::singleShot(500, this, SLOT(CheckImport()));
}

void ImportWalletDialog::CheckImport()
{
    if(import_result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        QTimer::singleShot(500, this, SLOT(CheckImport()));
        return;
    }

    bool success = import_result.get();
    if(!success) {
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
