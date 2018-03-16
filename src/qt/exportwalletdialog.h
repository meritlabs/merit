#ifndef MERIT_QT_EXPORTWALLETDIALOG_H
#define MERIT_QT_EXPORTWALLETDIALOG_H

#include <QDialog>

class WalletModel;

namespace Ui {
    class ExportWalletDialog;
}

class ExportWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportWalletDialog(QWidget *parent, WalletModel *model);
    ~ExportWalletDialog();

private Q_SLOTS:
    void onCancelClicked();
    void onShowClicked();

private:
    WalletModel *walletModel;
    Ui::ExportWalletDialog *ui;
    bool qrCodeIsVisible = false;

    void setQRCodeVisibility();
};

#endif
