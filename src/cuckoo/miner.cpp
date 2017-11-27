// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2016 John Tromp

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
#include <vector>

namespace cuckoo
{
bool FindProofOfWork(const uint256 hash, unsigned int nBits, uint8_t edgeBits, uint16_t edgesRatio, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());
    bool cycleFound = FindCycle(hash, edgeBits, edgesRatio, params.nCuckooProofSize, cycle);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (cycleFound && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

// TODO: check easiness here
bool VerifyProofOfWork(uint256 hash, unsigned int nBits, uint8_t edgeBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    if (cycle.size() != params.nCuckooProofSize) {
        return false;
    }

    if (edgeBits < MIN_EDGE_BITS || edgeBits > MAX_EDGE_BITS) {
        return false;
    }

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(hash, edgeBits, params.nCuckooProofSize, vCycle);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}

bool FindProofOfWorkAdvanced(const uint256 hash, unsigned int nBits, uint8_t edgeBits, uint16_t edgesRatio, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());
    bool cycleFound = FindCycleAdvanced(hash, edgeBits, edgesRatio, params.nCuckooProofSize, cycle);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (cycleFound && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

// TODO: udpdate this function if we wanna control memory usage size
uint8_t GetNextNodesBitsRequired(const CBlockIndex* pindexLast)
{
    return pindexLast->nEdgesBits;
}

uint8_t GetNextEdgesRatioRequired(const CBlockIndex* pindexLast)
{
    return pindexLast->nEdgesRatio;
}
}
