// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POW_H
#define MERIT_POW_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

Consensus::PoW GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
Consensus::PoW CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

#endif // MERIT_POW_H
