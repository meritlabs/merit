// Copyright (c) 2016-2017 The Merit Foundation developers
// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "modaloverlay.h"
#include "ui_modaloverlay.h"

#include "guiutil.h"

#include "chainparams.h"

#include <QResizeEvent>
#include <QPropertyAnimation>

ModalOverlay::ModalOverlay(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModalOverlay),
    bestHeaderHeight(0),
    bestHeaderDate(QDateTime()),
    layerIsVisible(false),
    userClosed(false)
{
    ui->setupUi(this);
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(closeClicked()));
    if (parent) {
        parent->installEventFilter(this);
        raise();
    }

    blockProcessTime.clear();
    setVisible(false);
}

ModalOverlay::~ModalOverlay()
{
    delete ui;
}

bool ModalOverlay::eventFilter(QObject * obj, QEvent * ev) {
    if (obj == parent()) {
        if (ev->type() == QEvent::Resize) {
            QResizeEvent * rev = static_cast<QResizeEvent*>(ev);
            resize(rev->size());
            if (!layerIsVisible)
                setGeometry(0, height(), width(), height());

        }
        else if (ev->type() == QEvent::ChildAdded) {
            raise();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool ModalOverlay::event(QEvent* ev) {
    if (ev->type() == QEvent::ParentAboutToChange) {
        if (parent()) parent()->removeEventFilter(this);
    }
    else if (ev->type() == QEvent::ParentChange) {
        if (parent()) {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}

void ModalOverlay::setKnownBestHeight(int count, const QDateTime& blockDate)
{
    if (count > bestHeaderHeight) {
        bestHeaderHeight = count;
        bestHeaderDate = blockDate;
    }
}

void ModalOverlay::tipUpdate(int count, const QDateTime& blockDate)
{
    QDateTime currentDate = QDateTime::currentDateTime();

    // keep a vector of samples of verification progress at height
    double verificationProgress = static_cast<double>(count) / static_cast<double>(bestHeaderHeight);
    qint64 currentMillis = currentDate.toMSecsSinceEpoch();
    blockProcessTime.push_front(qMakePair(currentMillis, verificationProgress));

    // show progress speed if we have more then one sample
    if (blockProcessTime.size() == AVG_WINDOW_LENGTH)
    {
        double progressDelta = 0;
        double progressPerHour = 0;
        qint64 timeDelta = 0;
        qint64 remainingMSecs = 0;

        QPair<qint64, double> sample = blockProcessTime.takeLast();
        timeDelta = currentMillis - sample.first;
        progressDelta = verificationProgress - sample.second;
        progressPerHour = progressDelta/static_cast<double>(timeDelta)*1000*3600;
        remainingMSecs = (bestHeaderHeight - count) * timeDelta / AVG_WINDOW_LENGTH;

        // show progress increase per hour
        ui->progressIncreasePerH->setText(QString::number(progressPerHour*100, 'f', 2)+"%");

        // show expected remaining time
        ui->expectedTimeLeft->setText(GUIUtil::formatNiceTimeOffset(remainingMSecs/1000));
    }

    // show the last block date
    ui->newestBlockDate->setText(blockDate.toString());

    // show the percentage done according to verificationProgress
    ui->percentageProgress->setText(QString::number(verificationProgress*100, 'f', 2)+"%");
    ui->progressBar->setValue(verificationProgress*100);

    if (!bestHeaderDate.isValid())
        // not syncing
        return;

    // estimate the number of headers left based on nPowTargetSpacing
    // and check if the gui is not aware of the best header (happens rarely)
    int estimateNumHeadersLeft = bestHeaderDate.secsTo(currentDate) / Params().GetConsensus().nPowTargetSpacing;
    bool hasBestHeader = bestHeaderHeight >= count;

    // show remaining number of blocks
    if (estimateNumHeadersLeft < HEADER_HEIGHT_DELTA_SYNC && hasBestHeader) {
        ui->numberOfBlocksLeft->setText(QString::number(bestHeaderHeight - count));
    } else {
        ui->numberOfBlocksLeft->setText(tr("Unknown. Syncing Headers (%1)...").arg(bestHeaderHeight));
        ui->expectedTimeLeft->setText(tr("Unknown..."));
    }
}

void ModalOverlay::toggleVisibility()
{
    showHide(layerIsVisible, true);
    if (!layerIsVisible)
        userClosed = true;
}

void ModalOverlay::showHide(bool hide, bool userRequested)
{
    if ( (layerIsVisible && !hide) || (!layerIsVisible && hide) || (!hide && userClosed && !userRequested))
        return;

    if (!isVisible() && !hide)
        setVisible(true);

    setGeometry(0, hide ? 0 : height(), width(), height());

    QPropertyAnimation* animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, hide ? 0 : this->height()));
    animation->setEndValue(QPoint(0, hide ? this->height() : 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = !hide;
}

void ModalOverlay::closeClicked()
{
    showHide(true);
    userClosed = true;
}
