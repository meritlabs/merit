// Copyright (c) 2017-2021 The Merit Foundation
#ifndef MERIT_QT_CALLBACK_H
#define MERIT_QT_CALLBACK_H

#include <QObject>

class Callback : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    virtual void call() = 0;
};

template <typename F>
class FunctionCallback : public Callback
{
    F f;

public:
    explicit FunctionCallback(F f_) : f(std::move(f_)) {}
    ~FunctionCallback() override {}
    void call() override { f(this); }
};

template <typename F>
FunctionCallback<F>* makeCallback(F f)
{
    return new FunctionCallback<F>(std::move(f));
}

#endif // MERIT_QT_CALLBACK_H
