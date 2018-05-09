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
#include "importwalletdialog.h"
#include "ui_importwalletdialog.h"

ImportWalletDialog::ImportWalletDialog(QWidget *parent, WalletModel *model) :
    QDialog(parent),
    walletModel(model),
    ui(new Ui::ImportWalletDialog)
{
    ui->setupUi(this);

    connect(ui->cancelButton, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
}

ImportWalletDialog::~ImportWalletDialog()
{
    delete ui;
}

void ImportWalletDialog::onCancelClicked()
{
}

