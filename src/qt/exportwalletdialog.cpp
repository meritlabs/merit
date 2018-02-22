#include <QPainter>
#include <QPixmap>

#include "config/merit-config.h" /* for USE_QRCODE */
#ifdef USE_QRCODE
    #include <qrencode.h>
#endif

#include "guiconstants.h"
#include "qrutil.h"
#include "exportwalletdialog.h"
#include "ui_exportwalletdialog.h"

ExportWalletDialog::ExportWalletDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExportWalletDialog)
{
    ui->setupUi(this);

#ifndef USE_QRCODE
    ui->pushButtonShow->setVisible(false);
    ui->lblQRCode->setVisible(false);
#endif

    connect(ui->pushButtonCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    connect(ui->pushButtonShow, SIGNAL(clicked()), this, SLOT(onShowClicked()));
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
    updateShowButtonText();
}

void ExportWalletDialog::setQRCodeVisibility()
{
    if(qrCodeIsVisible)
    {
        ui->lblQRCode->setText("");
        #ifdef USE_QRCODE
            QImage qrImage = qrutil::encodeQString(QString("{\"message\": \"Hello World\"}"));

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
        ui->lblQRCode->setText(tr("QR Code Hidden"));
    }
}

void ExportWalletDialog::updateShowButtonText()
{
    if(qrCodeIsVisible)
    {
        ui->pushButtonShow->setText(tr("Hide QR Code"));
    }
    else
    {
        ui->pushButtonShow->setText(tr("Show QR Code"));
    }
}