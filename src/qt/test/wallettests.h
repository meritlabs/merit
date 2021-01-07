// Copyright (c) 2017-2021 The Merit Foundation
#ifndef MERIT_QT_TEST_WALLETTESTS_H
#define MERIT_QT_TEST_WALLETTESTS_H

#include <QObject>
#include <QTest>

class WalletTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void walletTests();
};

#endif // MERIT_QT_TEST_WALLETTESTS_H
