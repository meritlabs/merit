#ifndef FASTSTART_H
#define FASTSTART_H

#include <QDialog>
#include <QtNetwork>

namespace Ui {
class FastStart;
}

struct SnapshotInfo
{
    QString url;
    quint64 pos = 0;
    quint64 size = 0;
};

class FastStart : public QDialog
{
    Q_OBJECT

public:
    explicit FastStart(QWidget *parent = 0);
    ~FastStart();

    static bool DoDownloadSnapshot();

public Q_SLOTS:
    void nextSlide();
    void endSlide();

    void SnapshotUrlDownloaded(QNetworkReply* reply);

    void SnapshotProgress(qint64 received, qint64 total);
    void SnapshotFinished();
    void SnapshotReadyRead();

private:
    void FigureOutSnapshotAndDownload();
    void DownloadSnapshotUrl();
    void DownloadSnapshot();

private:
    Ui::FastStart *ui;
    QString data_dir;
    SnapshotInfo snapshot;
    QNetworkAccessManager info_manager;
    QNetworkAccessManager snapshot_manager;
    QNetworkReply* snapshot_download;
    QFile snapshot_output;
};

#endif // FASTSTART_H
