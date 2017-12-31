// Copyright (c) 2016-2017 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POLICY_FEERATE_H
#define MERIT_POLICY_FEERATE_H

#include "amount.h"
#include "serialize.h"

#include <string>

extern const std::string CURRENCY_UNIT;

/**
 * Fee rate in satoshis per kilobyte: CAmount / kB
 */
class CFeeRate
{
private:
    CAmount nMicrosPerK; // unit is satoshis-per-1,000-bytes
public:
    /** Fee rate of 0 satoshis per kB */
    CFeeRate() : nMicrosPerK(0) { }
    explicit CFeeRate(const CAmount& _nMicrosPerK): nMicrosPerK(_nMicrosPerK) { }
    /** Constructor for a fee rate in satoshis per kB. The size in bytes must not exceed (2^63 - 1)*/
    CFeeRate(const CAmount& nFeePaid, size_t nBytes);
    /**
     * Return the fee in satoshis for the given size in bytes.
     */
    CAmount GetFee(size_t nBytes) const;
    /**
     * Return the fee in satoshis for a size of 1000 bytes
     */
    CAmount GetFeePerK() const { return GetFee(1000); }
    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK < b.nMicrosPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK > b.nMicrosPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK == b.nMicrosPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK <= b.nMicrosPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK >= b.nMicrosPerK; }
    friend bool operator!=(const CFeeRate& a, const CFeeRate& b) { return a.nMicrosPerK != b.nMicrosPerK; }
    CFeeRate& operator+=(const CFeeRate& a) { nMicrosPerK += a.nMicrosPerK; return *this; }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nMicrosPerK);
    }
};

#endif //  MERIT_POLICY_FEERATE_H
