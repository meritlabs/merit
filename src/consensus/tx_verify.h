// Copyright (c) 2017-2021 The Merit Foundation
// Copyright (c) 2017-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CONSENSUS_TX_VERIFY_H
#define MERIT_CONSENSUS_TX_VERIFY_H

#include "primitives/referral.h"

#include <stdint.h>
#include <vector>

class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;

namespace referral
{
    class ReferralsViewCache;
}

using ConfirmationSet = std::set<uint160>;

/** Transaction validation functions */

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, CValidationState& state);

namespace Consensus
{
/**
 * Check whether all inputs of this transaction are valid (no double spends and amounts)
 * This does not modify the UTXO set. This does not check scripts and sigs.
 * Preconditions: tx.IsCoinBase() is false.
 */
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight);

/**
 * Check weather all outputs of this transaction are valid (addresses are beaconed)
 * vExtraReferrals accepts and extra vector of referrals that can be looked up for beaconed addresses
 * e.g. in ConnectBlock it can be array of refs from new block and in mempool.check it can be array of refs from mempool
 */
bool CheckTxOutputs(
        const CTransaction& tx,
        CValidationState& state,
        const referral::ReferralsViewCache& referralsCache,
        const std::vector<referral::ReferralRef>& vExtraReferrals,
        const ConfirmationSet* block_invites = nullptr,
        bool check_mempool = false);
} // namespace Consensus

/** Auxiliary functions for transaction validation (ideally should not be exposed) */

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/**
 * Compute total signature operation cost of a transaction.
 * @param[in] tx     Transaction for which we are computing the cost
 * @param[in] inputs Map of previous transactions that have outputs we're spending
 * @param[out] flags Script verification flags
 * @return Total signature operation cost of tx
 */
int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, std::vector<int>* prevHeights, const CBlockIndex& block);

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair);
/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
 * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransaction &tx, std::vector<int>* prevHeights, const CBlockIndex& block);

#endif // MERIT_CONSENSUS_TX_VERIFY_H
