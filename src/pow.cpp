// Copyright (c) 2017-2021 The Merit Foundation
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

using Consensus::PoW;

PoW GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nBitsLimit = UintToArith256(params.powLimit.uHashLimit).GetCompact();

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

PoW CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    assert(pindexLast);

    if (params.fPowNoRetargeting)
        return PoW{pindexLast->nBits, pindexLast->nEdgeBits};

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
    if (edgeBitsAdjusted != pindexLast->nEdgeBits && params.sEdgeBitsAllowed.count(edgeBitsAdjusted)) {
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
