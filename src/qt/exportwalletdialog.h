#ifndef MERIT_QT_EXPORTWALLETDIALOG_H
#define MERIT_QT_EXPORTWALLETDIALOG_H

#include <QDialog>

namespace Ui {
    class ExportWalletDialog;
}

class ExportWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportWalletDialog(QWidget *parent = 0);
    ~ExportWalletDialog();

private Q_SLOTS:
    void onCancelClicked();
    void onShowClicked();

private:
    Ui::ExportWalletDialog *ui;
    bool qrCodeIsVisible = false;

    void setQRCodeVisibility();
    void updateShowButtonText();
};

#endif