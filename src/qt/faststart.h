#ifndef FASTSTART_H
#define FASTSTART_H

#include <QDialog>

namespace Ui {
class FastStart;
}

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
};

#endif // FASTSTART_H
