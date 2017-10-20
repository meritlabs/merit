// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CUCKOO_MINER_H
#define MERIT_CUCKOO_MINER_H

#include "chain.h"
#include "consensus/params.h"
#include "uint256.h"
#include <set>
#include <vector>

namespace cuckoo
{
/** Find cycle for block that satisfies the proof-of-work requirement specified by block hash */
bool FindProofOfWork(uint256 hash, unsigned int nBits, uint8_t nodesBits, uint8_t edgesRatio, std::set<uint32_t>& cycle, const Consensus::Params& params, double& time);

/** Check that provided cycle satisfies the proof-of-work requirement specified by block hash */
bool VerifyProofOfWork(uint256 hash, unsigned int nBits, uint8_t nodesBits, const std::set<uint32_t>& cycle, const Consensus::Params& params);


uint8_t GetNextEdgesRatioRequired(const CBlockIndex* pindexLast);
}

#endif // MERIT_CUCKOO_MINER_H
