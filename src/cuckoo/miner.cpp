// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2016 John Tromp

#include "miner.h"
#include "cuckoo.h"
#include "hash.h"
#include "pow.h"
#include <assert.h>
#include <numeric>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <ctime>

namespace cuckoo
{
bool FindProofOfWork(uint256 hash, unsigned int nBits, uint8_t nNodesBits, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());
    auto res = FindCycle(hash, nNodesBits, cycle, params.nCuckooProofSize, params.nCuckooDifficulty);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (res && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

bool VerifyProofOfWork(uint256 hash, unsigned int nBits, uint8_t nNodesBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.size() == params.nCuckooProofSize);

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(hash, nNodesBits, vCycle, params.nCuckooProofSize);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}
}
