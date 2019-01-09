// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "cuckoo/miner.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <algorithm>

using Consensus::PoW;

PoW GetNextWorkRequired(
        const CBlockIndex* pindexLast,
        const CBlockHeader* pblock,
        const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nBitsLimit = UintToArith256(params.powLimit.uHashLimit).GetCompact();

    if(pindexLast->nHeight >= params.lwma_blockheight) {

    }

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval(pindexLast->nHeight) != 0) {
        if (params.fPowAllowMinDifficultyBlocks) {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2 * 1 minute
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
                return PoW{nBitsLimit, params.powLimit.nEdgeBitsLimit};
            else {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval(pindexLast->nHeight) != 0 && pindex->nBits == nBitsLimit)
                    pindex = pindex->pprev;
                return PoW{pindex->nBits, pindex->nEdgeBits};
            }
        }
        return PoW{pindexLast->nBits, pindexLast->nEdgeBits};
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval(pindexLast->nHeight) - 1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

PoW CalculateNextWorkRequired(
        const CBlockIndex* pindexLast,
        int64_t nFirstBlockTime,
        const Consensus::Params& params)
{
    assert(pindexLast);

    if (params.fPowNoRetargeting) {
        return PoW{pindexLast->nBits, pindexLast->nEdgeBits};
    }

    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;

    auto pow_target_timespan = pindexLast->nHeight >= params.pog2_blockheight ?
        params.pog2_pow_target_timespan : params.nPowTargetTimespan;

    // Check if we can adjust nEdgeBits value
    uint8_t edgeBitsAdjusted = pindexLast->nEdgeBits;
    if (nActualTimespan < pow_target_timespan / params.nEdgeBitsTargetThreshold) {
        edgeBitsAdjusted++;
    }

    if (nActualTimespan > pow_target_timespan * params.nEdgeBitsTargetThreshold) {
        edgeBitsAdjusted--;
    }

    // Retarget nEdgeBits
    const auto edgebitsAllowed = pindexLast->nHeight >= params.lwma_blockheight ?
        params.lwma_sEdgeBitsAllowed : params.sEdgeBitsAllowed;
    if (edgeBitsAdjusted != pindexLast->nEdgeBits && edgebitsAllowed.count(edgeBitsAdjusted)) {
        LogPrintf("%s: adjusted edge bits accepted. prev bits: %u new bits: %u\n", __func__, pindexLast->nEdgeBits, edgeBitsAdjusted);
        return PoW{pindexLast->nBits, static_cast<uint8_t>(edgeBitsAdjusted)};
    }

    // Limit nBits adjustment step
    nActualTimespan = std::max(nActualTimespan, pow_target_timespan / 4);

    nActualTimespan = std::min(nActualTimespan, pow_target_timespan * 4);

    // Retarget nBits
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit.uHashLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);

    bnNew /= pow_target_timespan;
    bnNew *= nActualTimespan;

    bnNew = std::min(bnNew, bnPowLimit);

    LogPrintf("%s: adjusted nbits accepted. prev bits: %08x; new bits: %08x\n", __func__, pindexLast->nBits, bnNew.GetCompact());

    return PoW{bnNew.GetCompact(), pindexLast->nEdgeBits};
}

PoW CalculateLwmaNextWorkRequired(
        const CBlockIndex* pindexLast,
        const CBlockHeader* pblock,
        const Consensus::Params& params)
{
    if (params.fPowNoRetargeting) {
        return PoW{pindexLast->nBits, pindexLast->nEdgeBits};
    }

    const int height = pindexLast->nHeight + 1;
    int64_t actual_time_span = pindexLast->GetBlockTime() - pindexLast->GetAncestor(height - params.lwma_target_timespan)->GetBlockTime();

    auto pow_target_timespan = pindexLast->nHeight >= params.pog2_blockheight ?
        params.pog2_pow_target_timespan : params.nPowTargetTimespan;

    // Check if we can adjust nEdgeBits value
    uint8_t new_edge_bits = pindexLast->nEdgeBits;
    if (actual_time_span < pow_target_timespan / params.nEdgeBitsTargetThreshold) {
        new_edge_bits++;
    }

    if (actual_time_span > pow_target_timespan * params.nEdgeBitsTargetThreshold) {
        new_edge_bits--;
    }

    // Retarget nEdgeBits
    const auto edgebitsAllowed = pindexLast->nHeight >= params.lwma_blockheight ?
        params.lwma_sEdgeBitsAllowed : params.sEdgeBitsAllowed;
    if (new_edge_bits != pindexLast->nEdgeBits && edgebitsAllowed.count(new_edge_bits)) {
        LogPrintf("%s: adjusted edge bits accepted. prev bits: %u new bits: %u\n", __func__, pindexLast->nEdgeBits, new_edge_bits);
        return PoW{pindexLast->nBits, static_cast<uint8_t>(new_edge_bits)};
    }

    // otherwise retarget using nBits

    assert(height > params.lwma_target_timespan);
    const int f = (params.lwma_target_timespan + 1)/2*params.nPowTargetSpacing;

    const auto target_timespan_sq = std::pow(params.lwma_target_timespan, 2);

    arith_uint256 sum = 0;

    int64_t weighted_time = 0;
    int64_t actual_time = 0;
    int64_t weight = 1;

    //we will linearly weight each time difference
    for(int h = height - params.lwma_target_timespan; h < height; h++, weight++) {
        const auto* b = pindexLast->GetAncestor(h);
        assert(b);

        const auto* p = b->GetAncestor(h - 1);
        assert(p);

        auto time_diff = b->GetBlockTime() - p->GetBlockTime();
        actual_time += time_diff;

        weighted_time += time_diff * weight;

        arith_uint256 target;
        target.SetCompact(b->nBits);
        sum += target / (f * target_timespan_sq);
    }

    weighted_time = std::max(weighted_time, params.lwma_target_timespan * f / 3);

    arith_uint256 new_target = weighted_time * sum;

    return PoW{new_target.GetCompact(), pindexLast->nEdgeBits};
}


bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit.uHashLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
