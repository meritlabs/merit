// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "cuckoo/miner.h"
#include "hash.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "refmempool.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "validationinterface.h"

#include <algorithm>
#include <boost/thread.hpp>
#include <limits>
#include <queue>
#include <utility>

//////////////////////////////////////////////////////////////////////////////
//
// MeritMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockRef = 0;
uint64_t nLastBlockSize = 0;
uint64_t nLastBlockWeight = 0;

extern std::unique_ptr<CConnman> g_connman;
extern CCoinsViewCache *pcoinsTip;

const int MAX_NONCE = 0xfffff;

int64_t UpdateTime(
        CBlockHeader* pblock,
        const Consensus::Params& consensusParams,
        const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams).nBits;
    }

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options()
{
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    nTransactionsMaxSize = (DEFAULT_BLOCK_TRANSACTIONS_MAX_SIZE_SHARE * nBlockMaxSize) / 100;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
    // Limit size to between 1K and MAX_BLOCK_SERIALIZED_SIZE-1K for sanity:
    nBlockMaxSize = std::max<size_t>(1000, std::min<size_t>(MAX_BLOCK_SERIALIZED_SIZE - 1000, options.nBlockMaxSize));
    // Limit size to between 1K < blockMaxSize * SHARE (in percents) < blockMaxSize * MAX_TRANSACTIONS_SERIALIZED_SIZE_SHARE (90%):
    nTransactionsMaxSize = std::max<size_t>(1000, std::min<size_t>((nBlockMaxSize * MAX_TRANSACTIONS_SERIALIZED_SIZE_SHARE) / 100, options.nTransactionsMaxSize));

    // Whether we need to account for byte usage (in addition to weight usage)
    fNeedSizeAccounting = (nBlockMaxSize < MAX_BLOCK_SERIALIZED_SIZE - 1000);
}

static BlockAssembler::Options DefaultOptions(const CChainParams& params)
{
    // Block resource limits
    // If neither -blockmaxsize or -blockmaxweight is given, limit to DEFAULT_BLOCK_MAX_*
    // If only one is given, only restrict the specified resource.
    // If both are given, restrict both.
    BlockAssembler::Options options;
    auto nTransactionsMaxShare = DEFAULT_BLOCK_TRANSACTIONS_MAX_SIZE_SHARE;
    options.nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    bool fWeightSet = false;
    if (gArgs.IsArgSet("-blockmaxweight")) {
        options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
        options.nBlockMaxSize = MAX_BLOCK_SERIALIZED_SIZE;
        fWeightSet = true;
    }
    if (gArgs.IsArgSet("-blocktxsmaxsizeshare")) {
        nTransactionsMaxShare = gArgs.GetArg("-blocktxsmaxsizeshare", DEFAULT_BLOCK_TRANSACTIONS_MAX_SIZE_SHARE);
    }
    if (gArgs.IsArgSet("-blockmaxsize")) {
        options.nBlockMaxSize = gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
        if (!fWeightSet) {
            options.nBlockMaxWeight = options.nBlockMaxSize * WITNESS_SCALE_FACTOR;
        }
    }
    if (gArgs.IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n);
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }

    options.nTransactionsMaxSize = (nTransactionsMaxShare * options.nBlockMaxSize) / 100;

    assert(options.nBlockMaxSize >= options.nTransactionsMaxSize);

    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) :
    BlockAssembler(params, DefaultOptions(params)) {}

void BlockAssembler::resetBlock()
{
    txsInBlock.clear();
    refsInBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = true;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nBlockRef = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn)
{
    int64_t nTimeStart = GetTimeMicros();

    const auto& chain_params = chainparams.GetConsensus();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);       // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK(cs_main);
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chain_params);

    //Add a dummy coinbase invite as first invite in daedalus block
    if (pblock->IsDaedalus()) {
        pblock->invites.emplace_back();
    }

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ?
        nMedianTimePast : pblock->GetBlockTime();

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    {
        LOCK2(mempool.cs, mempoolReferral.cs);

        addPackageTxs(nPackagesSelected, nDescendantsUpdated);

        // add left referrals to the block after dependant transactions and referrals already added
        AddReferrals();
    }

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockRef = nBlockRef;
    nLastBlockSize = nBlockSize;
    nLastBlockWeight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;

    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;

    const auto previousBlockHash = pindexPrev->GetBlockHash();

    const auto subsidy = GetSplitSubsidy(nHeight, chain_params);
    assert(subsidy.miner > 0);
    assert(subsidy.ambassador > 0);

    /**
     * Merit splits the coinbase between the miners and the ambassadors of the system.
     * An ambassador is someone who brings a lot of people into the Merit system
     * via referrals. The rewards are given out in a lottery where the probability
     * of winning is based on an ambassadors referral network.
     */
    const auto lottery = RewardAmbassadors(
            nHeight,
            previousBlockHash,
            subsidy.ambassador,
            chain_params);
    assert(lottery.remainder >= 0);

    /**
     * Update the coinbase transaction vout with rewards.
     */
    PayAmbassadors(lottery, coinbaseTx);

    /**
     * The miner recieves their subsidy and any remaining subsidy that was left
     * over from paying the ambassadors. The reason there is a remaining subsidy
     * is because we use integer math.
     */
    const auto miner_subsidy = subsidy.miner + lottery.remainder;
    assert(miner_subsidy > 0);

    coinbaseTx.vout[0].nValue = nFees + miner_subsidy;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    CValidationState state;

    //Include invites if we are mining a daudalus block
    if (pblock->IsDaedalus()) {
        CMutableTransaction coinbaseInvites;
        coinbaseInvites.vin.resize(1);
        coinbaseInvites.vin[0].prevout.SetNull();
        coinbaseInvites.vin[0].scriptSig = CScript() << nHeight << OP_0;

        bool improved_lottery_on = nHeight >= chain_params.imp_invites_blockheight;

        //Improved invite lottery allows the miner to pay themselves an invite
        if(improved_lottery_on) {
            coinbaseInvites.vout.resize(1);
            coinbaseInvites.vout[0].scriptPubKey = scriptPubKeyIn;
            coinbaseInvites.vin[0].scriptSig = CScript() << nHeight << OP_0;
        }

        coinbaseInvites.nVersion = CTransaction::INVITE_VERSION;

        assert(pcoinsTip);

        DebitsAndCredits debits_and_credits;
        pog::InviteRewards invites;

        auto it = pblock->invites.begin();
        // skip coinbase invite
        while (++it != pblock->invites.end()) {
            GetDebitsAndCredits(debits_and_credits, **it, *pcoinsTip);
        }

        RewardInvites(
                nHeight,
                pindexPrev,
                previousBlockHash,
                *pcoinsTip,
                debits_and_credits,
                chain_params,
                state,
                invites);

        if (invites.empty() && !improved_lottery_on) {
            // remove empty coinbase 
            pblock->invites.erase(pblock->invites.begin());
        } else {
            DistributeInvites(invites, coinbaseInvites);
            pblock->invites[0] = MakeTransactionRef(std::move(coinbaseInvites));
        }
    }

    pblocktemplate->vchCoinbaseCommitment =
        GenerateCoinbaseCommitment(*pblock, pindexPrev, chain_params);
    pblocktemplate->vTxFees[0] = -nFees;


    uint64_t nSerializeSize = GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);

    LogPrintf(
            "CreateNewBlock(): total size: %u block weight: %u txs: %u "
            "fees: %ld sigops: %d refs: %d\n",
            nSerializeSize,
            GetBlockWeight(*pblock),
            nBlockTx,
            nFees,
            nBlockSigOpsCost,
            nBlockRef);

    auto pow = GetNextWorkRequired(pindexPrev, pblock, chain_params);

    // Fill in header
    pblock->hashPrevBlock = previousBlockHash;
    UpdateTime(pblock, chain_params, pindexPrev);
    pblock->nBits = pow.nBits;
    pblock->nNonce = 0;
    pblock->nEdgeBits = pow.nEdgeBits;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(
                strprintf(
                    "%s: TestBlockValidity failed: %s",
                    __func__,
                    FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint(
            BCLog::BENCH,
            "CreateNewBlock() packages: %.2fms (%d packages, %d updated "
            "descendants), validity: %.2fms (total %.2fms)\n",
            0.001 * (nTime1 - nTimeStart),
            nPackagesSelected, nDescendantsUpdated,
            0.001 * (nTime2 - nTime1),
            0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end();) {
        // Only test txs not already in the block
        if (txsInBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

void BuildConfirmationSet(const CTxMemPool::setEntries& testSet, ConfirmationSet& confirmations)
{
    for (const auto& txentry : testSet) {
        if (txentry->GetSharedEntryValue()->IsInvite()) {
            BuildConfirmationSet(txentry->GetSharedEntryValue(), confirmations);
        }
    }
}


bool BlockAssembler::CheckReferrals(
        CTxMemPool::setEntries& testSet,
        referral::ReferralRefs& candidate_referrals)
{
    ConfirmationSet confirmations;
    BuildConfirmationSet(txsInBlock, confirmations);
    BuildConfirmationSet(testSet, confirmations);

    // test all referrals are signed
    for (const auto& referral: candidate_referrals) {
        if (!CheckReferralSignature(*referral)) {
            return false;
        }

        if (pblock->IsDaedalus()) {
            // Check package for confirmation for give referral
            if (confirmations.count(referral->GetAddress()) == 0) {
                debug("WARNING: Referral confirmation not found: %s",
                        CMeritAddress{referral->addressType, referral->GetAddress()}.ToString());
                return false;
            }
        }
    }

    // test all tx's outputs are beaconed and confirmed
    for (const auto it : testSet) {
        const auto tx = it->GetEntryValue();

        CValidationState dummy;
        if (!Consensus::CheckTxOutputs(tx, dummy, *prefviewcache, candidate_referrals)) {
            return false;
        }
    }

    return true;
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
// - serialized size (in case -blockmaxsize is in use)
bool BlockAssembler::TestPackageContent(
        const CTxMemPool::setEntries& transactions,
        const referral::ReferralRefs& referrals)
{
    uint64_t nPotentialBlockSize = nBlockSize; // only used with fNeedSizeAccounting
    for (const auto it : transactions) {
        if (!IsFinalTx(it->GetEntryValue(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetEntryValue().HasWitness())
            return false;
        if (fNeedSizeAccounting) {

            uint64_t nTxSize =
                ::GetSerializeSize(
                        it->GetEntryValue(),
                        SER_NETWORK,
                        PROTOCOL_VERSION);

            // share block size by transactions and referrals
            if (nPotentialBlockSize + nTxSize >= nTransactionsMaxSize) {
                return false;
            }
            nPotentialBlockSize += nTxSize;
        }
    }

    if (fNeedSizeAccounting) {
        for (const auto& it: referrals) {
            uint64_t nRefSize = ::GetSerializeSize(*it, SER_NETWORK, PROTOCOL_VERSION);

            // share block size by transactions and referrals
            if (nPotentialBlockSize + nRefSize >= nBlockMaxSize) {
                return false;
            }
            nPotentialBlockSize += nRefSize;
        }
    }

    return true;
}

void BlockAssembler::AddTransactionToBlock(CTxMemPool::txiter iter)
{
    const auto& tx = iter->GetEntryValue();
    if(tx.IsInvite()) {
        debug("Miner Assembler: adding invite transaction to block");
        pblock->invites.emplace_back(iter->GetSharedEntryValue());
    } else {
        pblock->vtx.emplace_back(iter->GetSharedEntryValue());
        pblocktemplate->vTxFees.push_back(iter->GetFee());
        nFees += iter->GetFee();
    }

    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    auto txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    if (fNeedSizeAccounting) {
        nBlockSize += txSize;
    }

    nBlockWeight += iter->GetWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    txsInBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
            CFeeRate(iter->GetModifiedFee(), iter->GetSize()).ToString(),
            tx.GetHash().ToString());
    }
}

void BlockAssembler::AddReferralToBlock(referral::ReferralTxMemPool::RefIter iter)
{
    auto ref = iter->GetSharedEntryValue();
    assert(ref);

    if (refsInBlock.count(iter)) {
        debug("\t%s: Referral %s is already in block\n", __func__,
                ref->GetHash().GetHex());
        return;
    }

    if (!mempoolReferral.Exists(ref->parentAddress)
            && !prefviewdb->GetReferral(ref->parentAddress)) {
        return;
    }

    pblock->m_vRef.push_back(ref);

    if (fNeedSizeAccounting) {
        nBlockSize += iter->GetSize();
    }

    nBlockWeight += iter->GetWeight();
    refsInBlock.insert(iter);

    ++nBlockRef;
}

int BlockAssembler::UpdatePackagesForAdded(
        const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set& mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (const CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(
        CTxMemPool::txiter it,
        indexed_modified_transaction_set& mapModifiedTx,
        CTxMemPool::setEntries& failedTx)
{
    assert(it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || txsInBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(
        const CTxMemPool::setEntries& package,
        CTxMemPool::txiter entry,
        std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

void BlockAssembler::AddReferrals()
{
    uint64_t nPotentialBlockSize = nBlockSize; // only used with fNeedSizeAccounting

    ConfirmationSet confirmations;
    BuildConfirmationSet(txsInBlock, confirmations);

    for (auto it = mempoolReferral.mapRTx.begin(); it != mempoolReferral.mapRTx.end(); it++) {
        const auto ref = it->GetSharedEntryValue();

        if (refsInBlock.count(it)) {
            debug("\t%s: referral for %s is already in block", __func__, CMeritAddress{ref->addressType, ref->GetAddress()}.ToString());
            continue;
        }

        // test all referrals are signed
        if (!CheckReferralSignature(*ref)) {
            continue;
        }

        if (pblock->IsDaedalus()) {
            // Check package for confirmation for give referral
            if (confirmations.count(ref->GetAddress()) == 0) {
                debug("\t%s: confirmation for %s not found. Skipping", __func__, CMeritAddress{ref->addressType, ref->GetAddress()}.ToString());
                continue;
            }
        }

        uint64_t nRefSize = it->GetSize();

        if (fNeedSizeAccounting) {
            // share block size by transactions and referrals
            if (nPotentialBlockSize + nRefSize >= nBlockMaxSize) {
                break;
            }
            nPotentialBlockSize += nRefSize;
        }

        // Check mempoolForParent
        // If we don't find the parent in the mempool (it's also not in block at this point)
        // Look in the blockchain.
        if (!mempoolReferral.Exists(ref->parentAddress)
                && !prefviewdb->GetReferral(ref->parentAddress)) {
            continue;
        }

        pblock->m_vRef.push_back(ref);
        if (fNeedSizeAccounting) {
            nBlockSize = nPotentialBlockSize;
        }

        nBlockWeight += it->GetWeight();

        ++nBlockRef;
    }
}

bool BlockAssembler::GetCandidatePacakageReferrals(
    const SetRefEntries& package_referrals,
    referral::ReferralRefs& candidate_referrals)
{
    std::set<referral::ReferralRef> candidate_in_block_referrals;

    // get referrals in already added to block and referrals from current package
    // to one set to skip duplicates
    std::transform(refsInBlock.begin(), refsInBlock.end(),
        std::inserter(candidate_in_block_referrals, candidate_in_block_referrals.end()),
        [](const referral::ReferralTxMemPool::RefIter& entryit) {
            return entryit->GetSharedEntryValue();
        });

    std::transform(package_referrals.begin(), package_referrals.end(),
        std::inserter(candidate_in_block_referrals, candidate_in_block_referrals.end()),
        [](const referral::ReferralTxMemPool::RefIter& entryit) {
            return entryit->GetSharedEntryValue();
        });

    // move referrals from set to vector and build referral tree
    // of the current candidate block
    referral::ReferralRefs sorted_referrals;
    sorted_referrals.insert(
        sorted_referrals.begin(),
        candidate_in_block_referrals.begin(),
        candidate_in_block_referrals.end());

    // if we couldn't build a tree this txs/refs package assumed invalid
    if (!prefviewdb->OrderReferrals(sorted_referrals)) {
        return false;
    }

    for (const auto& referral: sorted_referrals) {
        auto ref_iter = mempoolReferral.mapRTx.find(referral->GetHash());
        assert(ref_iter != mempoolReferral.mapRTx.end());

        // add to resulting vector if referral found both in candidate block
        // and current referrals package
        if (package_referrals.count(ref_iter) > 0) {
            candidate_referrals.push_back(referral);
        }
    }

    return true;
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(txsInBlock, mapModifiedTx);

    auto mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
            SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are txsInBlock, and mapModifiedTx shouldn't
        // contain anything that is txsInBlock.
        assert(!txsInBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors() + iter->GetSizeReferrals();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors + modit->nSizeReferrals;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (!iter->GetEntryValue().IsInvite() &&
                packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                                                                     nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;

        mempool.CalculateMemPoolAncestors(
                *iter,
                ancestors,
                nNoLimit,
                nNoLimit,
                nNoLimit,
                nNoLimit,
                dummy,
                false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        referral::ReferralTxMemPool::setEntries referrals;
        mempool.CalculateMemPoolAncestorsReferrals(ancestors, referrals);

        CTxMemPool::setEntries confirmations;

        // Add confirmations only for daedalus block
        if (pblock->IsDaedalus()) {
            // TODO: test block size limits and update confirmations selection
            // when transactions weight is close to limit
            mempool.CalculateReferralsConfirmations(referrals, ancestors);
            onlyUnconfirmed(ancestors);
        }

        referral::ReferralRefs cadidate_referrals;

        // Test if referrals tree is valid, all tx's have required referrals
        // and all tx's are Final
        if (!GetCandidatePacakageReferrals(referrals, cadidate_referrals) ||
            !CheckReferrals(ancestors, cadidate_referrals) ||
            !TestPackageContent(ancestors, cadidate_referrals)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }
        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (const auto& it : sortedEntries) {
            AddTransactionToBlock(it);

            mapModifiedTx.erase(it);
        }

        std::vector<referral::ReferralTxMemPool::RefIter> sorted_referral_entries;

        std::transform(cadidate_referrals.begin(), cadidate_referrals.end(), std::back_inserter(sorted_referral_entries),
            [](const referral::ReferralRef& referral) {
                return mempoolReferral.mapRTx.find(referral->GetHash());
            });

        for (const auto& it: sorted_referral_entries) {
            AddReferralToBlock(it);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(
        CBlock* pblock,
        const CBlockIndex* pindexPrev,
        unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("MeritMiner: generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(chainparams, shared_pblock, true, nullptr, false))
        return error("MeritMiner: ProcessNewBlock, block not accepted");

    return true;
}

struct MinerContext {
    std::atomic<bool>& alive;
    int pow_threads;
    int threads_number;
    int nonces_per_thread;
    const CChainParams& chainparams;
    std::shared_ptr<CReserveScript>& coinbase_script;
    ctpl::thread_pool& pool;
};

void MinerWorker(int thread_id, MinerContext& ctx)
{
    auto start_nonce = thread_id * ctx.nonces_per_thread;
    unsigned int nExtraNonce = 0;

    while (ctx.alive) {
        if (ctx.chainparams.MiningRequiresPeers()) {
            // Busy-wait for the network to come online so we don't waste
            // time mining n an obsolete chain. In regtest mode we expect
            // to fly solo.
            if (!g_connman) {
                throw std::runtime_error(
                        "Peer-to-peer functionality missing or disabled");
            }

            do {
                bool fvNodesEmpty =
                    g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0;

                if (!fvNodesEmpty && !IsInitialBlockDownload())
                    break;

                g_connman->ResetMiningStats();
                MilliSleep(1000);
            } while (ctx.alive);
        }

        if(g_connman) {
            g_connman->InitMiningStats();
        }

        //
        // Create new block
        //
        unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrev = chainActive.Tip();

        std::unique_ptr<CBlockTemplate> pblocktemplate{
            BlockAssembler(Params()).CreateNewBlock(ctx.coinbase_script->reserveScript)};

        if (!pblocktemplate.get()) {
            LogPrintf(
                    "Error in MeritMiner: Keypool ran out, please call "
                    "keypoolrefill before restarting the mining thread\n");
            return;
        }

        CBlock* pblock = &pblocktemplate->block;
        assert(pblock);

        pblock->nNonce = start_nonce;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        LogPrintf(
                "%d: Running MeritMiner with %u transactions, %u invites, and %u referrals "
                "in block (%u bytes)\n",
            thread_id,
            pblock->vtx.size(),
            pblock->invites.size(),
            pblock->m_vRef.size(),
            ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

        //
        // Search
        //
        int64_t nStart = GetTimeMillis();
        auto nonces_checked = 0;
        arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
        uint256 hash;
        std::set<uint32_t> cycle;

        while (ctx.alive) {
            // Check if something found
            nonces_checked++;

            if (cuckoo::FindProofOfWorkAdvanced(
                        pblock->GetHash(),
                        pblock->nBits,
                        pblock->nEdgeBits,
                        cycle,
                        ctx.chainparams.GetConsensus(),
                        ctx.pow_threads,
                        ctx.pool)) {
                // Found a solution
                pblock->sCycle = cycle;

                auto cycleHash = SerializeHash(cycle);

                LogPrintf("%d: MeritMiner:\n", thread_id);
                LogPrintf(
                        "\n\n\nproof-of-work found within %8.3f seconds \n"
                        "\tblock hash: %s\n\tnonce: %d\n\tcycle hash: %s\n\ttarget: %s\n\n\n",
                    static_cast<double>(GetTimeMillis() - nStart) / 1e3,
                    pblock->GetHash().GetHex(),
                    pblock->nNonce,
                    cycleHash.GetHex(),
                    hashTarget.GetHex());

                ProcessBlockFound(pblock, ctx.chainparams);
                ctx.coinbase_script->KeepScript();

                // In regression test mode, stop mining after a block is found.
                if (ctx.chainparams.MineBlocksOnDemand())
                    throw boost::thread_interrupted();

                break;
            }

            // Check for stop or if block needs to be rebuilt
            if (!ctx.alive) {
                break;
            }

            // Regtest mode doesn't require peers
            if ((!g_connman || g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) &&
                    ctx.chainparams.MiningRequiresPeers()) {
                break;
            }

            if (pblock->nNonce >= MAX_NONCE) {
                break;
            }

            if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast &&
                    (GetTimeMillis() - nStart) / 1e3 > ctx.chainparams.MininBlockStaleTime()) {
                break;
            }

            if (pindexPrev != chainActive.Tip()) {
                LogPrintf("%d: Active chain tip changed. Breaking block lookup\n", thread_id);
                break;
            }

            // Update nTime every few seconds
            if (UpdateTime(pblock, ctx.chainparams.GetConsensus(), pindexPrev) < 0) {
                // Recreate the block if the clock has run backwards,
                // so that we can use the correct time.
                break;
            }

            if (ctx.chainparams.GetConsensus().fPowAllowMinDifficultyBlocks) {
                // Changing pblock->nTime can change work required on testnet:
                hashTarget.SetCompact(pblock->nBits);
            }

            pblock->nNonce++;

            if (pblock->nNonce % ctx.nonces_per_thread == 0) {
                pblock->nNonce += ctx.nonces_per_thread * (ctx.threads_number - 1);
            }
        }

        if (ctx.alive && g_connman) {
            g_connman->AddCheckedNonces(nonces_checked);
        }
    }

    LogPrintf("MeritMiner pool #%d terminated\n", thread_id);
}

void static MeritMiner(
        std::shared_ptr<CReserveScript> coinbase_script,
        const CChainParams& chainparams,
        int pow_threads,
        int bucket_size,
        int bucket_threads)
{
    assert(coinbase_script);
    RenameThread("merit-miner");

    if (bucket_threads < 1) {
        bucket_threads = 1;
    }

    if (bucket_size == 0) {
        bucket_size = MAX_NONCE / bucket_threads;
    }

    ctpl::thread_pool pool(bucket_threads + bucket_threads * pow_threads);
    std::atomic<bool> alive{true};

    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbase_script || coinbase_script->reserveScript.empty()) {
            throw std::runtime_error(
                    "No coinbase script available "
                    "(mining requires a wallet)");
        }

        LogPrintf("Running MeritMiner with %d pow threads, %d nonces per bucket and %d buckets in parallel.\n", pow_threads, bucket_size, bucket_threads);

        for (int t = 0; t < bucket_threads; t++) {
            MinerContext ctx{
                alive,
                pow_threads,
                bucket_threads,
                bucket_size,
                chainparams,
                coinbase_script,
                pool
            };

            pool.push(MinerWorker, ctx);
        }

        while (true) {
            boost::this_thread::interruption_point();
        }
    } catch (const boost::thread_interrupted&) {
        LogPrintf("MeritMiner terminated\n");
        alive = false;
        pool.stop();

        throw;
    } catch (const std::runtime_error& e) {
        LogPrintf("MeritMiner runtime error: %s\n", e.what());
        gArgs.ForceSetArg("-mine", 0);
        pool.stop();

        return;
    }
}

void GenerateMerit(bool mine, int pow_threads, int bucket_size, int bucket_threads, const CChainParams& chainparams)
{
    static boost::thread* minerThread = nullptr;

    if (pow_threads < 0) {
        pow_threads = std::thread::hardware_concurrency() / 2;
        bucket_threads = 2;
    }

    if (minerThread != nullptr) {
        minerThread->interrupt();
        delete minerThread;
        minerThread = nullptr;
    }

    if (pow_threads == 0 || bucket_threads == 0 || !mine) {
        if(g_connman) {
            g_connman->ResetMiningStats();
        }
        return;
    }

    std::shared_ptr<CReserveScript> coinbase_script;
    GetMainSignals().ScriptForMining(coinbase_script);

    if(!coinbase_script) {
        throw std::runtime_error("unable to generate a coinbase script for mining");
    }

    minerThread = new boost::thread(
            &MeritMiner,
            coinbase_script,
            chainparams,
            pow_threads,
            bucket_size,
            bucket_threads);
}
