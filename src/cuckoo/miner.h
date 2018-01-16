// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CUCKOO_MINER_H
#define MERIT_CUCKOO_MINER_H

#include "chain.h"
#include "consensus/params.h"
#include "uint256.h"
#include "ctpl/ctpl.h"
#include <set>
#include <vector>

namespace cuckoo
{

/**
 * Find cycle for block that satisfies the proof-of-work requirement specified
 * by block hash
 */
bool FindProofOfWork(
        uint256 hash,
        unsigned int nBits,
        uint8_t edgeBits,
        std::set<uint32_t>& cycle,
        const Consensus::Params& params);

/**
 * Check that provided cycle satisfies the proof-of-work requirement specified
 * by block hash
 */
bool VerifyProofOfWork(
        uint256 hash,
        unsigned int nBits,
        uint8_t edgeBits,
        const std::set<uint32_t>& cycle,
        const Consensus::Params& params);

/**
 * Find cycle for block that satisfies the proof-of-work requirement
 * specified by block hash with advanced edge trimming and matrix solver
 */
bool FindProofOfWorkAdvanced(
        uint256 hash,
        unsigned int nBits,
        uint8_t edgeBits,
        std::set<uint32_t>& cycle,
        const Consensus::Params& params,
        ctpl::thread_pool&);
}

#endif // MERIT_CUCKOO_MINER_H
