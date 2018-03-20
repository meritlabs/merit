#include "faststart.h"
#include "ui_faststart.h"
#include "util.h"

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QSettings>
#include <QStringList>
#include <QUrl>

namespace 
{
    const int SLIDE_TRANSITION_SECONDS = 15;
    const std::string DEFAULT_URL_URL = "https://mempko.com/merit/current";
}

bool FastStart::DoDownloadSnapshot()
{
    FastStart fastart;
    if(!fastart.exec()) {
        /* Cancel clicked */
        return false;
    }
    return true;
}

QString StatusText(QString status)
{
    return 
        QString{"<html><head/><body><p align=\"center\"><span style=\" color:#7a90a7;\">"} +
        status +
        QString{"</span></p></body></html>"};
}

FastStart::FastStart(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FastStart)
{
    ui->setupUi(this);
    ui->overviewSlides->setCurrentIndex(0);

    data_dir = QString::fromStdString(gArgs.GetArg("-datadir", GetDefaultDataDir().string()));

    connect(
            &info_manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(SnapshotUrlDownloaded(QNetworkReply*)));

    FigureOutSnapshotAndDownload();

    //start slideshow
    QTimer::singleShot(1000 * SLIDE_TRANSITION_SECONDS, this, SLOT(endSlide()));
}

FastStart::~FastStart()
{
    snapshot_output.close();
    delete ui;
}

void FastStart::FigureOutSnapshotAndDownload() 
{
    QSettings settings;
    ui->progressBar->setMaximum(0);
    snapshot.url = settings.value("snapshoturl", "").toString();
    snapshot.size = settings.value("snapshotsize", qulonglong{0}).toULongLong();

    if(snapshot.url.isEmpty()) {
        DownloadSnapshotUrl();
    } else {
        DownloadSnapshot();
    }
}

void FastStart::DownloadSnapshotUrl() 
{
     QUrl url_url = QString::fromStdString(gArgs.GetArg("-snapshoturl", DEFAULT_URL_URL));
     
     QNetworkRequest request{url_url};
     info_manager.get(request);
}

void FastStart::SnapshotUrlDownloaded(QNetworkReply* reply) 
{
    if(reply->error()) {
        ui->statusLabel->setText(
                StatusText(tr("Error Downloading Snapshot info from") + 
                " " + reply->url().toString()));
    } else {
        snapshot.url = QString::fromStdString(reply->readAll().toStdString()).trimmed();
        QSettings settings;
        DownloadSnapshot();
    }
    reply->deleteLater();
}

void FastStart::DownloadSnapshot() 
{
    QUrl url = snapshot.url;
    ui->statusLabel->setText(StatusText(tr("Downloading:") + " " + url.toString()));

    QString file_name = data_dir + "/snapshot.zip";
    snapshot_output.setFileName(file_name);
    if(!snapshot_output.open(QIODevice::ReadWrite)) {
        ui->statusLabel->setText(StatusText(tr("Unable to open the snapshot file")));
        return;
    }

    snapshot.pos = snapshot_output.size();

    QNetworkRequest request{url};

    if(snapshot.pos > 0) {
        ui->progressBar->setMaximum(snapshot.size);
        ui->progressBar->setValue(snapshot.pos);

        snapshot_output.seek(snapshot.pos);
        QString range("bytes=" + QString::number(snapshot.pos) + "-");
        request.setRawHeader("Range", range.toLatin1());
    }

    snapshot_download = snapshot_manager.get(request);

    connect(snapshot_download, SIGNAL(downloadProgress(qint64,qint64)),
            SLOT(SnapshotProgress(qint64,qint64)));
    connect(snapshot_download, SIGNAL(finished()),
            SLOT(SnapshotFinished()));
    connect(snapshot_download, SIGNAL(readyRead()),
            SLOT(SnapshotReadyRead()));
}

void FastStart::SnapshotProgress(qint64 received, qint64 size)
{
    if(snapshot.size == 0) {
        snapshot.size = size;
    }

    received += snapshot.pos;
    size += snapshot.pos;

    ui->progressBar->setMaximum(size);
    ui->progressBar->setValue(received);

    QSettings settings;
    settings.setValue("snapshoturl", snapshot.url);
    settings.setValue("snapshotsize", snapshot.size);
}

void FastStart::SnapshotFinished()
{
    assert(snapshot_download != nullptr);

    snapshot_output.close();

    if(snapshot_download->error()) {
        ui->statusLabel->setText(StatusText(tr("There was an error downloading the snapshot")));
    } else {
        ui->progressBar->setMaximum(0);
        ui->statusLabel->setText(StatusText(tr("Extracting the Snapshot...")));
    }

    snapshot_download->deleteLater();
}

void FastStart::SnapshotReadyRead()
{
    assert(snapshot_download != nullptr);
    snapshot_output.write(snapshot_download->readAll());
}

void FastStart::nextSlide() 
{
    int next = (ui->overviewSlides->currentIndex() + 1) % ui->overviewSlides->count();
    ui->overviewSlides->setCurrentIndex(next);

    QGraphicsOpacityEffect *e = new QGraphicsOpacityEffect(this);
    ui->overviewSlides->setGraphicsEffect(e);
    QPropertyAnimation* a = new QPropertyAnimation(e, "opacity");
    a->setDuration(500);
    a->setStartValue(0);
    a->setEndValue(1);
    a->setEasingCurve(QEasingCurve::OutQuad);
    a->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(1000 * SLIDE_TRANSITION_SECONDS, this, SLOT(endSlide()));
}

void FastStart::endSlide() 
{
    QGraphicsOpacityEffect *e = new QGraphicsOpacityEffect(this);
    ui->overviewSlides->setGraphicsEffect(e);
    QPropertyAnimation* a = new QPropertyAnimation(e, "opacity");
    a->setDuration(500);
    a->setStartValue(1);
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutQuad);
    a->start(QAbstractAnimation::DeleteWhenStopped);
    QTimer::singleShot(600, this, SLOT(nextSlide()));
}
