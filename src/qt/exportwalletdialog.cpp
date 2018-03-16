#include <QPainter>
#include <QPixmap>
#include <QString>

#include "config/merit-config.h" /* for USE_QRCODE */
#ifdef USE_QRCODE
    #include <qrencode.h>
#endif

#include "base58.h"
#include "guiconstants.h"
#include "qrutil.h"
#include "walletmodel.h"
#include "exportwalletdialog.h"
#include "ui_exportwalletdialog.h"

ExportWalletDialog::ExportWalletDialog(QWidget *parent, WalletModel *model) :
    QDialog(parent),
    walletModel(model),
    ui(new Ui::ExportWalletDialog)
{
    ui->setupUi(this);

#ifndef USE_QRCODE
    ui->lblQRCode->setVisible(false);
#endif

    connect(ui->pushButtonCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    connect(ui->lblQRCode, SIGNAL(clicked()), this, SLOT(onShowClicked()));
}

ExportWalletDialog::~ExportWalletDialog()
{
    delete ui;
}

void ExportWalletDialog::onCancelClicked()
{
    reject();
}

void ExportWalletDialog::onShowClicked()
{
    qrCodeIsVisible = !qrCodeIsVisible;
    setQRCodeVisibility();
}

void ExportWalletDialog::setQRCodeVisibility()
{
    if(qrCodeIsVisible)
    {
        ui->lblQRCode->setText("");
    #ifdef USE_QRCODE
        bool livenet = Params().NetworkIDString() == CBaseChainParams::MAIN;
        QString mnemonic = walletModel->getMnemonic();
        QString qrCode = QStringLiteral("1|") +
            mnemonic +
            QStringLiteral("|m/44'/") +
            (livenet ? QStringLiteral("0'") : QStringLiteral("1'")) +
            QStringLiteral("/0'|false");
        QImage qrImage = qrutil::encodeQString(qrCode);

        QImage qrBackImage = QImage(QR_IMAGE_SIZE, QR_IMAGE_SIZE, QImage::Format_RGB32);
        qrBackImage.fill(0xffffff);

        QPainter painter(&qrBackImage);
        painter.drawImage(0, 0, qrImage.scaled(QR_IMAGE_SIZE, QR_IMAGE_SIZE));
        painter.end();

        ui->lblQRCode->setPixmap(QPixmap::fromImage(qrBackImage));
    #endif
    }
    else
    {
        ui->lblQRCode->setPixmap(QPixmap());
        ui->lblQRCode->setText(tr("Click to reveal your QR Code."));
    }
}
