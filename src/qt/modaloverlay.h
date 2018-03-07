// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_MODALOVERLAY_H
#define MERIT_QT_MODALOVERLAY_H

#include <QDateTime>
#include <QWidget>

//! The required delta of headers to the estimated number of available headers until we show the IBD progress
static constexpr int HEADER_HEIGHT_DELTA_SYNC = 240;
//! number of block processing times to average to estimate sync time remaining
static constexpr int AVG_WINDOW_LENGTH = 100;


namespace Ui {
    class ModalOverlay;
}

/** Modal overlay to display information about the chain-sync state */
class ModalOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ModalOverlay(QWidget *parent);
    ~ModalOverlay();

public Q_SLOTS:
    void tipUpdate(int count, const QDateTime& blockDate);
    void setKnownBestHeight(int count, const QDateTime& blockDate);

    void toggleVisibility();
    // will show or hide the modal layer
    void showHide(bool hide = false, bool userRequested = false);
    void closeClicked();
    bool isLayerVisible() const { return layerIsVisible; }
    void allowHide();
    void nextSlide();
    void endSlide();

protected:
    bool eventFilter(QObject * obj, QEvent * ev);
    bool event(QEvent* ev);

private:
    void setProgressBusy();
    void setProgressActive();

    Ui::ModalOverlay *ui;
    int bestHeaderHeight; //best known height (based on the headers)
    int startCount;
    QDateTime bestHeaderDate;
    QVector<QPair<qint64, int> > block_time_samples;
    bool layerIsVisible;
    bool userClosed;
    bool canHide = false;
};

#endif // MERIT_QT_MODALOVERLAY_H
