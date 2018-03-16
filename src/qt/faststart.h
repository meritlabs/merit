#ifndef FASTSTART_H
#define FASTSTART_H

#include <QDialog>

namespace Ui {
class FastStart;
}

struct SnapshotInfo
{
    std::string url;
    size_t position;
    size_t size;
};

class FastStart : public QDialog
{
    Q_OBJECT

public:
    explicit FastStart(QWidget *parent = 0);
    ~FastStart();

public Q_SLOTS:
    void nextSlide();
    void endSlide();

private:
    Ui::FastStart *ui;
    std::string data_dir;
};

#endif // FASTSTART_H
