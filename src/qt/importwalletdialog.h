#ifndef MERIT_QT_IMPORTWALLETDIALOG_H
#define MERIT_QT_IMPORTWALLETDIALOG_H

#include <QDialog>

class WalletModel;

namespace Ui {
    class ImportWalletDialog;
}

class ImportWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportWalletDialog(QWidget *parent, WalletModel *model);
    ~ImportWalletDialog();

private Q_SLOTS:
    void OnCancelClicked();
    void ImportWallet();
    void UpdateImportButton();
    void DoImport();

private:
    WalletModel *walletModel;
    Ui::ImportWalletDialog *ui;
};

#endif
