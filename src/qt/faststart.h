#ifndef FASTSTART_H
#define FASTSTART_H

#include <QDialog>
#include <QtNetwork>
#include <QSettings>

namespace Ui {
class FastStart;
}

struct SnapshotInfo
{
    enum State {CHOICE=0, GETINFO, DOWNLOAD, EXTRACT, DONE} state = CHOICE;
    QString url;
    quint64 pos = 0;
    quint64 size = 0;
};

class FastStart : public QDialog
{
    Q_OBJECT

public:
    explicit FastStart(const QString& data_dir, QWidget *parent = 0);
    ~FastStart();

    static bool DoDownloadSnapshot();
    bool Failed() const { return failed;}

public Q_SLOTS:
    void nextSlide();
    void endSlide();

    void SnapshotUrlDownloaded(QNetworkReply* reply);

    void SnapshotProgress(qint64 received, qint64 total);
    void SnapshotFinished();
    void SnapshotReadyRead();
    void SnapshotChoiceClicked();
    void PeersChoiceClicked();
    void TryAgain();

private:
    void Start();
    void ShowDownload();
    void ShowChoice();
    void DownloadSnapshotUrl();
    void DownloadSnapshot();
    void ExtractSnapshot();

private:
    bool failed;
    Ui::FastStart *ui;
    QString data_dir;
    SnapshotInfo snapshot;
    QNetworkAccessManager info_manager;
    QNetworkAccessManager snapshot_manager;
    QNetworkReply* snapshot_download;
    QFile snapshot_output;
    QTime download_time;
    QSettings settings;
};

#endif // FASTSTART_H
