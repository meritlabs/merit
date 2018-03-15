#include "faststart.h"
#include "ui_faststart.h"

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>

namespace 
{
    int SLIDE_TRANSITION_SECONDS = 15;
}

FastStart::FastStart(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FastStart)
{
    ui->setupUi(this);

    //start slideshow
    QTimer::singleShot(1000 * SLIDE_TRANSITION_SECONDS, this, SLOT(endSlide()));
}

FastStart::~FastStart()
{
    delete ui;
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
