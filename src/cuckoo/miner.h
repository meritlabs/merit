/*
 * Cuckoo Cycle, a memory-hard proof-of-work
 * Copyright (c) 2013-2018 John Tromp
 * Copyright (c) 2017-2018 The Merit Foundation developers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the The FAIR MINING License and, alternatively,
 * GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See LICENSE.md for more details.
 **/

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
        size_t nThreads,
        ctpl::thread_pool& pool);
}

#endif // MERIT_CUCKOO_MINER_H
