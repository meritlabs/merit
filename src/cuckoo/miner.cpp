// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2016 John Tromp

#include "miner.h"
#include "cuckoo.h"
#include "hash.h"
#include "pow.h"
#include "utiltime.h"
#include <assert.h>
#include <numeric>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

namespace cuckoo
{
bool FindProofOfWork(const uint256 hash, unsigned int nBits, uint8_t nodesBits, uint8_t edgesRatio, std::set<uint32_t>& cycle, const Consensus::Params& params, double& time)
{
    assert(cycle.empty());
    auto begin = GetTime();
    auto res = FindCycle(hash, nodesBits, edgesRatio, params.nCuckooProofSize, cycle);
    auto end = GetTime();

    time = end - begin;

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (res && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

bool VerifyProofOfWork(uint256 hash, unsigned int nBits, uint8_t nodesBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.size() == params.nCuckooProofSize);

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(hash, nodesBits, params.nCuckooProofSize, vCycle);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}

// TODO: udpdate this function if we wanna control memory usage size
uint8_t GetNextNodesBitsRequired(const CBlockIndex* pindexLast)
{
    return pindexLast->nNodesBits;
}

uint8_t GetNextEdgesRatioRequired(const CBlockIndex* pindexLast)
{
    return pindexLast->nEdgesRatio;
}
}
