// Copyright (c) 2017-2021 The Merit Foundation
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "feerate.h"

#include "tinyformat.h"

const std::string CURRENCY_UNIT = "MRT";

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nBytes_)
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    if (nSize > 0)
        nMicrosPerK = nFeePaid * 1000 / nSize;
    else
        nMicrosPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nBytes_) const
{
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    CAmount nFee = nMicrosPerK * nSize / 1000;

    if (nFee == 0 && nSize != 0) {
        if (nMicrosPerK > 0)
            nFee = CAmount(1);
        if (nMicrosPerK < 0)
            nFee = CAmount(-1);
    }

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nMicrosPerK / COIN, nMicrosPerK % COIN, CURRENCY_UNIT);
}
