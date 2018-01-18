// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "chainparams.h"
#include "hash.h"
#include "random.h"
#include "streams.h"
#include "txmempool.h"
#include "refmempool.h"
#include "validation.h"
#include "util.h"

#include <unordered_map>
#include <algorithm>

uint64_t BlockHeaderAndShortIDs::GetShortID(const uint256& hash) const
{
    static_assert(SHORT_ID_LENGTH == 6, "short transaction ids calculation assumes 6-byte ids");
    return SipHashUint256(m_short_idk0, m_short_idk1, hash) & 0xffffffffffffL;
}

BlockHeaderAndShortIDs::BlockHeaderAndShortIDs(const CBlock& block, bool fUseWTXID) :
        m_nonce(GetRand(std::numeric_limits<uint64_t>::max())),
        m_short_tx_ids(block.vtx.size() - 1),
        m_short_ref_ids(block.m_vRef.size()),
        m_prefilled_txn(1), m_prefilled_inv(1), 
        header(block)
{
    if(block.IsDaedalus()) {
        m_short_inv_ids.resize(block.invites.size() - 1);
    }

    FillShortIDSelector();
    //TODO: Use our mempool prior to block acceptance to predictively fill more than just the coinbase
    m_prefilled_txn[0] = {0, block.vtx[0]};

    if(block.IsDaedalus()) {
        m_prefilled_inv[0] = {0, block.invites[0]};
    }

    std::transform(
            std::begin(block.vtx) + 1, std::end(block.vtx), std::begin(m_short_tx_ids),
            [this,fUseWTXID](const CTransactionRef& tx) {
                return GetShortID(fUseWTXID ? tx->GetWitnessHash() : tx->GetHash());
            });

    if(block.IsDaedalus()) {
        std::transform(
                std::begin(block.invites) + 1, std::end(block.invites), std::begin(m_short_inv_ids),
                [this,fUseWTXID](const CTransactionRef& inv) {
                return GetShortID(fUseWTXID ? inv->GetWitnessHash() : inv->GetHash());
                });
    }

    std::transform(
            std::begin(block.m_vRef), std::end(block.m_vRef), std::begin(m_short_ref_ids),
            [this,fUseWTXID](const referral::ReferralRef& ref) {
                return GetShortID(ref->GetHash());
            });
}

void BlockHeaderAndShortIDs::FillShortIDSelector() const {
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << header << m_nonce;
    CSHA256 hasher;
    hasher.Write((unsigned char*)&(*stream.begin()), stream.end() - stream.begin());
    uint256 shorttxidhash;
    hasher.Finalize(shorttxidhash.begin());
    m_short_idk0 = shorttxidhash.GetUint64(0);
    m_short_idk1 = shorttxidhash.GetUint64(1);
}

ReadStatus InitTxnData(
        std::vector<CTransactionRef>& txn_available,
        size_t block_count,
        const std::vector<PrefilledTransaction>& prefilled_txn,
        const ShortIds& block_short_tx_ids,
        const BlockHeaderAndShortIDs& cmpctblock,
        const ExtraTransactions& extra_txn, 
        CTxMemPool* txn_pool,
        size_t& mempool_txn_count,
        size_t& prefilled_txn_count,
        size_t& extra_txn_count)
{
    txn_available.resize(block_count);

    int32_t lastprefilledindex = -1;
    for (size_t i = 0; i < prefilled_txn.size(); i++) {
        if (prefilled_txn[i].value->IsNull())
            return READ_STATUS_INVALID;

        lastprefilledindex += prefilled_txn[i].index + 1; //index is a uint16_t, so can't overflow here
        if (lastprefilledindex > std::numeric_limits<uint16_t>::max())
            return READ_STATUS_INVALID;
        if ((uint32_t)lastprefilledindex > block_short_tx_ids.size() + i) {
            // If we are inserting a tx at an index greater than our full list of block_short_tx_ids
            // plus the number of prefilled txn we've inserted, then we have txn for which we
            // have neither a prefilled txn or a shorttxid!
            return READ_STATUS_INVALID;
        }
        txn_available[lastprefilledindex] = prefilled_txn[i].value;
    }
    prefilled_txn_count = prefilled_txn.size();

    // Calculate map of txids -> positions and check mempool to see what we have (or don't)
    // Because well-formed cmpctblock messages will have a (relatively) uniform distribution
    // of short IDs, any highly-uneven distribution of elements can be safely treated as a
    // READ_STATUS_FAILED.
    std::unordered_map<uint64_t, uint16_t> short_tx_ids(block_short_tx_ids.size());
    uint16_t index_offset = 0;
    for (size_t i = 0; i < block_short_tx_ids.size(); i++) {
        while (txn_available[i + index_offset])
            index_offset++;
        short_tx_ids[block_short_tx_ids[i]] = i + index_offset;
        // To determine the chance that the number of entries in a bucket exceeds N,
        // we use the fact that the number of elements in a single bucket is
        // binomially distributed (with n = the number of m_short_tx_ids S, and p =
        // 1 / the number of buckets), that in the worst case the number of buckets is
        // equal to S (due to std::unordered_map having a default load factor of 1.0),
        // and that the chance for any bucket to exceed N elements is at most
        // buckets * (the chance that any given bucket is above N elements).
        // Thus: P(max_elements_per_bucket > N) <= S * (1 - cdf(binomial(n=S,p=1/S), N)).
        // If we assume blocks of up to 16000, allowing 12 elements per bucket should
        // only fail once per ~1 million block transfers (per peer and connection).
        if (short_tx_ids.bucket_size(short_tx_ids.bucket(block_short_tx_ids[i])) > 12)
            return READ_STATUS_FAILED;
    }
    // TODO: in the shortid-collision case, we should instead request both transactions
    // which collided. Falling back to full-block-request here is overkill.
    if (short_tx_ids.size() != block_short_tx_ids.size())
        return READ_STATUS_FAILED; // Short ID collision

    std::vector<bool> have_txn(txn_available.size());
    {
        LOCK(txn_pool->cs);
        const std::vector<std::pair<uint256, CTxMemPool::txiter> >& vTxHashes = txn_pool->vTxHashes;
        for (size_t i = 0; i < vTxHashes.size(); i++) {
            uint64_t shortid = cmpctblock.GetShortID(vTxHashes[i].first);
            std::unordered_map<uint64_t, uint16_t>::iterator idit = short_tx_ids.find(shortid);
            if (idit != short_tx_ids.end()) {
                if (!have_txn[idit->second]) {
                    txn_available[idit->second] = vTxHashes[i].second->GetSharedEntryValue();
                    have_txn[idit->second]  = true;
                    mempool_txn_count++;
                } else {
                    // If we find two mempool txn that match the short id, just request it.
                    // This should be rare enough that the extra bandwidth doesn't matter,
                    // but eating a round-trip due to FillBlock failure would be annoying
                    if (txn_available[idit->second]) {
                        txn_available[idit->second].reset();
                        mempool_txn_count--;
                    }
                }
            }
            // Though ideally we'd continue scanning for the two-txn-match-shortid case,
            // the performance win of an early exit here is too good to pass up and worth
            // the extra risk.
            if (mempool_txn_count == short_tx_ids.size())
                break;
        }
    }

    for (size_t i = 0; i < extra_txn.size(); i++) {
        uint64_t shortid = cmpctblock.GetShortID(extra_txn[i].first);
        std::unordered_map<uint64_t, uint16_t>::iterator idit = short_tx_ids.find(shortid);
        if (idit != short_tx_ids.end()) {
            if (!have_txn[idit->second]) {
                txn_available[idit->second] = extra_txn[i].second;
                have_txn[idit->second]  = true;
                mempool_txn_count++;
                extra_txn_count++;
            } else {
                // If we find two mempool/extra txn that match the short id, just
                // request it.
                // This should be rare enough that the extra bandwidth doesn't matter,
                // but eating a round-trip due to FillBlock failure would be annoying
                // Note that we don't want duplication between extra_txn and mempool to
                // trigger this case, so we compare witness hashes first
                if (txn_available[idit->second] &&
                        txn_available[idit->second]->GetWitnessHash() != extra_txn[i].second->GetWitnessHash()) {
                    txn_available[idit->second].reset();
                    mempool_txn_count--;
                    extra_txn_count--;
                }
            }
        }
        // Though ideally we'd continue scanning for the two-txn-match-shortid case,
        // the performance win of an early exit here is too good to pass up and worth
        // the extra risk.
        if (mempool_txn_count == short_tx_ids.size())
            break;
    }

    return READ_STATUS_OK;
}

ReadStatus PartiallyDownloadedBlock::InitData(
        const BlockHeaderAndShortIDs& cmpctblock,
        const ExtraTransactions& extra_txn_and_inv,
        const ExtraReferrals& extra_ref) {


    ExtraTransactions extra_txn;
    ExtraTransactions extra_inv;

    std::copy_if(extra_txn_and_inv.begin(), extra_txn_and_inv.end(),
            std::back_inserter(extra_txn),
            [](const ExtraTransaction& tx) {
                return !tx.second->IsInvite();
            });

    std::copy_if(extra_txn_and_inv.begin(), extra_txn_and_inv.end(),
            std::back_inserter(extra_inv),
            [](const ExtraTransaction& tx) {
                return tx.second->IsInvite();
            });

    auto txn_and_inv_size = 
        cmpctblock.m_short_tx_ids.size() 
        + cmpctblock.m_prefilled_txn.size()
        + cmpctblock.m_short_inv_ids.size()
        + cmpctblock.m_prefilled_inv.size();

    if (cmpctblock.header.IsNull() || (cmpctblock.m_short_tx_ids.empty() && cmpctblock.m_prefilled_txn.empty())) {
        return READ_STATUS_INVALID;
    }

    if (txn_and_inv_size > MAX_BLOCK_WEIGHT / MIN_SERIALIZABLE_TRANSACTION_WEIGHT) {
        return READ_STATUS_INVALID;
    }

    assert(header.IsNull() && m_txn_available.empty());
    assert(header.IsNull() && m_refs_available.empty());
    assert(header.IsNull() && m_inv_available.empty());

    header = cmpctblock.header;

    m_refs_available.resize(cmpctblock.BlockRefCount());

    const auto txn_read_status = InitTxnData(
            m_txn_available,
            cmpctblock.BlockTxCount(),
            cmpctblock.m_prefilled_txn,
            cmpctblock.m_short_tx_ids,
            cmpctblock,
            extra_txn, 
            m_txn_pool,
            m_mempool_txn_count,
            m_prefilled_txn_count,
            m_extra_txn_count);

    if(txn_read_status != READ_STATUS_OK) {
        return txn_read_status;
    }

    const auto inv_read_status = InitTxnData(
            m_inv_available,
            cmpctblock.BlockInvCount(),
            cmpctblock.m_prefilled_inv,
            cmpctblock.m_short_inv_ids,
            cmpctblock,
            extra_inv, 
            m_txn_pool,
            m_mempool_inv_count,
            m_prefilled_inv_count,
            m_extra_inv_count);

    if(inv_read_status != READ_STATUS_OK) {
        return inv_read_status;
    }

    LogPrint(BCLog::CMPCTBLOCK, "Initialized PartiallyDownloadedBlock for block %s using a cmpctblock of size %lu\n", cmpctblock.header.GetHash().ToString(), GetSerializeSize(cmpctblock, SER_NETWORK, PROTOCOL_VERSION));

    return READ_STATUS_OK;
}

bool PartiallyDownloadedBlock::IsTxAvailable(size_t index) const {
    assert(!header.IsNull());
    assert(index < m_txn_available.size());
    return m_txn_available[index] != nullptr;
}

bool PartiallyDownloadedBlock::IsInviteAvailable(size_t index) const {
    assert(!header.IsNull());
    assert(index < m_inv_available.size());
    return m_inv_available[index] != nullptr;
}

ReadStatus FillTxns(
        std::vector<CTransactionRef>& vtx, 
        const std::vector<CTransactionRef>& txn_available,
        const MissingTransactions& vtx_missing)
{
    vtx.resize(txn_available.size());

    size_t tx_missing_offset = 0;
    for (size_t i = 0; i < txn_available.size(); i++) {
        if (!txn_available[i]) {
            if (vtx_missing.size() <= tx_missing_offset) {
                return READ_STATUS_INVALID;
            }
            vtx[i] = vtx_missing[tx_missing_offset++];
        } else {
            vtx[i] = std::move(txn_available[i]);
        }
    }

    if (vtx_missing.size() != tx_missing_offset) {
        return READ_STATUS_INVALID;
    }

    return READ_STATUS_OK;
}

ReadStatus PartiallyDownloadedBlock::FillBlock(
        CBlock& block,
        const MissingTransactions& vtx_missing,
        const MissingTransactions& inv_missing,
        const MissingReferrals& ref_missing) {

    assert(!header.IsNull());
    uint256 hash = header.GetHash();
    block = header;

    const auto fill_txn_status = FillTxns(block.vtx, m_txn_available, vtx_missing);
    if(fill_txn_status != READ_STATUS_OK) {
        return fill_txn_status;
    }

    const auto fill_inv_status = FillTxns(block.invites, m_inv_available, inv_missing);
    if(fill_inv_status != READ_STATUS_OK) {
        return fill_inv_status;
    }

    // Make sure we can't call FillBlock again.
    header.SetNull();
    m_txn_available.clear();

    CValidationState state;
    if (!CheckBlock(block, state, Params().GetConsensus())) {
        // TODO: We really want to just check merkle tree manually here,
        // but that is expensive, and CheckBlock caches a block's
        // "checked-status" (in the CBlock?). CBlock should be able to
        // check its own merkle root and cache that check.
        if (state.CorruptionPossible())
            return READ_STATUS_FAILED; // Possible Short ID collision
        return READ_STATUS_CHECKBLOCK_FAILED;
    }

    LogPrint(BCLog::CMPCTBLOCK, "Successfully reconstructed block %s with %lu txn prefilled, %lu txn from mempool (incl at least %lu from extra pool) and %lu txn requested\n", hash.ToString(), m_prefilled_txn_count, m_mempool_txn_count, m_extra_txn_count, vtx_missing.size());
    if (vtx_missing.size() < 5) {
        for (const auto& tx : vtx_missing) {
            LogPrint(BCLog::CMPCTBLOCK, "Reconstructed block %s required tx %s\n", hash.ToString(), tx->GetHash().ToString());
        }
    }

    return READ_STATUS_OK;
}
