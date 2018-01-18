#ifndef MERIT_QT_ENTERUNLOCKCODE_H
#define MERIT_QT_ENTERUNLOCKCODE_H

#include "walletmodel.h"
#include <QWidget>

namespace Ui {
    class EnterUnlockCode;
}

/** Modal overlay to display information about the chain-sync state */
class EnterUnlockCode : public QWidget
{
    Q_OBJECT

public:
    explicit EnterUnlockCode(QWidget *parent);
    ~EnterUnlockCode();

Q_SIGNALS:
    void walletReferred();

public Q_SLOTS:
    void submitButtonClicked();
    void setModel(WalletModel *model);
    // will show or hide the modal layer
    void showHide(bool hide = false, bool userRequested = false);
    bool isLayerVisible() const { return layerIsVisible; }

protected:
    bool eventFilter(QObject * obj, QEvent * ev);
    bool event(QEvent* ev);

private:
    Ui::EnterUnlockCode *ui;
    WalletModel *walletModel;
    bool layerIsVisible;
    bool userClosed;

    void InvalidAddressMessageBox();
};

#endif // MERIT_QT_ENTERUNLOCKCODE_H
