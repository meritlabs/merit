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
bool FindProofOfWork(const uint256 hash, unsigned int nBits, uint8_t edgeBits, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());
    bool cycleFound = FindCycle(hash, edgeBits, params.nCuckooProofSize, cycle);

    auto cycleHash = SerializeHash(cycle);

    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    printf("%s: cycle  hash: %s\n", __func__, cycleHash.GetHex().c_str());
    printf("%s: target hash: %s\n", __func__, bnTarget.GetHex().c_str());

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit.uHashLimit)) {
        printf("%s: simple check not passed\n", __func__);
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        printf("%s: target check not passed\n", __func__);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (cycleFound && ::CheckProofOfWork(cycleHash, nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

bool VerifyProofOfWork(uint256 hash, unsigned int nBits, uint8_t edgeBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
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

bool FindProofOfWorkAdvanced(const uint256 hash, unsigned int nBits, uint8_t edgeBits, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());
    bool cycleFound = FindCycleAdvanced(hash, edgeBits, params.nCuckooProofSize, cycle);

    if (cycleFound) {
        auto cycleHash = SerializeHash(cycle);

        bool fNegative;
        bool fOverflow;
        arith_uint256 bnTarget;

        bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

        printf("%s: cycle  hash: %s\n", __func__, cycleHash.GetHex().c_str());
        printf("%s: target hash: %s\n", __func__, bnTarget.GetHex().c_str());

        // Check range
        if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit.uHashLimit)) {
            printf("%s: simple check not passed\n", __func__);
        }

        // Check proof of work matches claimed amount
        if (UintToArith256(cycleHash) > bnTarget)
            printf("%s: target check not passed\n", __func__);

        // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
        if (::CheckProofOfWork(cycleHash, nBits, params)) {
            return true;
        }
    }
    cycle.clear();

    return false;
}
}
