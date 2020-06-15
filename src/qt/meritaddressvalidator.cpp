// Copyright (c) 2017-2020 The Merit Foundation
// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "meritaddressvalidator.h"
#include "qt/walletmodel.h"

#include "base58.h"

/* Base58 characters are:
     "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

  This is:
  - All numbers except for '0'
  - All upper-case letters except for 'I' and 'O'
  - All lower-case letters except for 'l'
*/

extern CTxDestination LookupDestination(const std::string& address);
bool Valid(const std::string& input, WalletModel* model)
{
    assert(model);
    auto dest = LookupDestination(input);

    CMeritAddress address;
    address.Set(dest);

    return address.IsValid() 
        && model->AddressBeaconed(address)
        && model->AddressConfirmed(address);
}

MeritAddressEntryValidator::MeritAddressEntryValidator(QObject *parent, WalletModel* m) :
    QValidator(parent), model{m}
{
    assert(model);
}

QValidator::State MeritAddressEntryValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    assert(model);

    // Empty address is "intermediate" input
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    // Correction
    for (int idx = 0; idx < input.size();)
    {
        bool removeChar = false;
        QChar ch = input.at(idx);
        // Corrections made are very conservative on purpose, to avoid
        // users unexpectedly getting away with typos that would normally
        // be detected, and thus sending to the wrong address.
        switch(ch.unicode())
        {
        // Qt categorizes these as "Other_Format" not "Separator_Space"
        case 0x200B: // ZERO WIDTH SPACE
        case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE
            removeChar = true;
            break;
        default:
            break;
        }

        // Remove whitespace
        if (ch.isSpace())
            removeChar = true;

        // To next character
        if (removeChar)
            input.remove(idx, 1);
        else
            ++idx;
    }

    // Validation
    return QValidator::Acceptable;
}

MeritAddressCheckValidator::MeritAddressCheckValidator(QObject *parent, WalletModel* m) :
    QValidator(parent), model{m}
{
    assert(model);
}

QValidator::State MeritAddressCheckValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);

    // Validate the passed Merit address
    if (Valid(input.toStdString(), model)) {
        return QValidator::Acceptable;
    }

    return QValidator::Invalid;
}
