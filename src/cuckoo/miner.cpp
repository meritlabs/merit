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

// assume EDGEBITS < 31
#define NNODES (2 * NEDGES)

#define MAXPATHLEN 8192

namespace cuckoo
{
bool FindProofOfWork(uint256 hash, int nonce, unsigned int nBits, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());

    int ratio = params.nCuckooDifficulty;

    assert(ratio >= 0 && ratio <= 100);
    uint64_t difficulty = ratio * (uint64_t)NNODES / 100;

    printf("Looking for %d-cycle on cuckoo%d(\"%s\") with %d%% edges and %d nonce\n", params.nCuckooProofSize, EDGEBITS + 1, hash.GetHex().c_str(), ratio, nonce);

    CuckooCtx ctx(reinterpret_cast<char*>(hash.begin()), hash.size(), nonce, difficulty);

    auto res = FindCycle(&ctx, cycle, params.nCuckooProofSize);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (res && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

bool VerifyProofOfWork(uint256 hash, int nonce, unsigned int nBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.size() == params.nCuckooProofSize);

    siphash_keys keys;

    const char* header = reinterpret_cast<char*>(hash.begin());
    uint32_t headerlen = hash.size();

    SetKeys(nonceToHeader(header, headerlen, nonce), headerlen, &keys);

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(vCycle, &keys, params.nCuckooProofSize);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}
}
