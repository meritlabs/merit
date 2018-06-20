// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CONSENSUS_PARAMS_H
#define MERIT_CONSENSUS_PARAMS_H

#include "uint256.h"
#include <map>
#include <set>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_GENESIS,
    DEPLOYMENT_DAEDALUS,
    DEPLOYMENT_IMP_INVITES,
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;

    int start_block = 0;
    int end_block = 0;
};

struct PoW {
    uint32_t nBits;
    uint8_t nEdgeBits;
};

struct PoWLimit {
    uint256 uHashLimit;
    uint8_t nEdgeBitsLimit;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    uint160 genesis_address;

    int nSubsidyHalvingInterval;
    uint32_t nBlocksToMaturity;

    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    PoWLimit powLimit;
    std::set<uint8_t> sEdgeBitsAllowed;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing; // target time for a block
    int64_t nPowTargetTimespan; // target time for nBits adjustments
    int64_t nEdgeBitsTargetThreshold; // threshold for nEdgeBits adjustments
    int64_t DifficultyAdjustmentInterval(int height) const { 
        return (height >= pog2_blockheight ? pog2_pow_target_timespan : nPowTargetTimespan)
            / nPowTargetSpacing; 
    }
    int64_t ambassador_percent_cut;
    uint64_t total_winning_ambassadors;
    uint64_t initial_block_reward;
    uint64_t max_lottery_reservoir_size;
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;
    /** Cuckoo cycle length */
    uint8_t nCuckooProofSize;

    /** Daedalus parameters **/
    int daedalus_max_invites_per_block;
    int daedalus_block_window;
    int daedalus_min_one_invite_for_every_x_blocks;
    int daedalus_max_outstanding_invites_per_address;
    
    /** Bug Fix heights */
    int safer_alias_blockheight;

    /** Improved Invites */
    int imp_invites_blockheight;
    int imp_block_window;
    int imp_min_one_invite_for_every_x_blocks;
    int imp_miner_reward_for_every_x_blocks;
    std::vector<double> imp_weights;

    /** PoG version 2 */
    int pog2_blockheight;
    uint64_t pog2_total_winning_ambassadors;
    int64_t pog2_ambassador_percent_cut;
    int64_t pog2_pow_target_timespan;
    int pog2_new_distribution_age;

};
} // namespace Consensus

#endif // MERIT_CONSENSUS_PARAMS_H
