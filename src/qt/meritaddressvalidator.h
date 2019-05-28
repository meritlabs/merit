// Copyright (c) 2017-2019 The Merit Foundation developers
// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_QT_MERITADDRESSVALIDATOR_H
#define MERIT_QT_MERITADDRESSVALIDATOR_H

#include <QValidator>

class WalletModel;

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class MeritAddressEntryValidator : public QValidator
{
    Q_OBJECT

    WalletModel* model = nullptr;
public:
    explicit MeritAddressEntryValidator(QObject *parent, WalletModel* model);

    State validate(QString &input, int &pos) const;
};

/** Merit address widget validator, checks for a valid merit address.
 */
class MeritAddressCheckValidator : public QValidator
{
    Q_OBJECT

    WalletModel* model = nullptr;
public:
    explicit MeritAddressCheckValidator(QObject *parent, WalletModel* model);

    State validate(QString &input, int &pos) const;

};

#endif // MERIT_QT_MERITADDRESSVALIDATOR_H
