// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_AMOUNT_H
#define MERIT_AMOUNT_H

#include <stdint.h>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;


static const CAmount COIN = 100000000;
static const CAmount CENT = 1000000;

/**
 * Converts merit to micros
 */
constexpr CAmount operator "" _merit(unsigned long long int a)
{
    return a * COIN;
}

/** No amount larger than this (in micro) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Merit
 * currently happens to be less than 100,000,000 MRT for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static const CAmount MAX_MONEY = 100000000_merit;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  MERIT_AMOUNT_H
