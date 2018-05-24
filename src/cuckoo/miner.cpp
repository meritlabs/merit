/*
 * Cuckoo Cycle, a memory-hard proof-of-work
 * Copyright (c) 2013-2018 John Tromp
 * Copyright (c) 2017-2018 The Merit Foundation developers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the The FAIR MINING License and, alternatively,
 * GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See src/cuckoo/LICENSE.md for more details.
 **/

#include "miner.h"
#include "cuckoo.h"
#include "mean_cuckoo.h"

#include "consensus/consensus.h"
#include "hash.h"
#include "pow.h"

#include <assert.h>
#include <numeric>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "util.h"
#include <vector>

namespace cuckoo
{

bool VerifyProofOfWork(
        uint256 hash,
        unsigned int nBits,
        uint8_t edgeBits,
        const std::set<uint32_t>& cycle,
        const Consensus::Params& params)
{

    if (cycle.size() != params.nCuckooProofSize) {
        return false;
    }

    if (!params.sEdgeBitsAllowed.count(edgeBits)) {
        return false;
    }

    assert(edgeBits >= MIN_EDGE_BITS && edgeBits <= MAX_EDGE_BITS);

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(hash, edgeBits, params.nCuckooProofSize, vCycle);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}

bool FindProofOfWorkAdvanced(
    const uint256 hash,
    unsigned int nBits,
    uint8_t edgeBits,
    std::set<uint32_t>& cycle,
    const Consensus::Params& params,
    size_t nThreads,
    bool& cycleFound,
    ctpl::thread_pool& pool)
{
    assert(cycle.empty());
    cycleFound =
        FindCycleAdvanced(hash, edgeBits, params.nCuckooProofSize, cycle, nThreads, pool);

    if (cycleFound && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

}
