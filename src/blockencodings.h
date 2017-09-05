// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_ENCODINGS_H
#define BITCOIN_BLOCK_ENCODINGS_H

#include "primitives/block.h"

#include <memory>

class CTxMemPool;
class ReferralTxMemPool;

// Dumb helper to handle CTransaction compression at serialize-time
struct TransactionCompressor {
private:
    CTransactionRef& tx;
public:
    explicit TransactionCompressor(CTransactionRef& txIn) : tx(txIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tx); //TODO: Compress tx encoding
    }
};

struct ReferralCompressor {
private:
    ReferralRef& ref;
public:
    explicit ReferralCompressor(ReferralRef& in) : ref(in) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ref); //TODO: Compress tx encoding
    }
};

template <typename Stream, typename Operation>
void ReadCompressedIndices(Stream& s, Operation ser_action, uint64_t size, std::vector<uint16_t>& decompressed)
{
    decompressed.resize(size);

    //read in deltas
    for (auto& index : decompressed) {
        uint64_t index64 = 0;
        READWRITE(COMPACTSIZE(index64));
        if (index64 > std::numeric_limits<uint16_t>::max())
            throw std::ios_base::failure("index overflowed 16 bits");
        index = index64;
    }

    //de-delta
    uint16_t offset = 0;
    for (auto& index : decompressed) {
        if (static_cast<uint64_t>(index) + static_cast<uint64_t>(offset) > std::numeric_limits<uint16_t>::max())
            throw std::ios_base::failure("index overflowed 16 bits");
        index = index + offset;
        offset = index + 1;
    }
}

template <typename Stream, typename Operation>
void WriteCompressedIndices(Stream& s, Operation ser_action, const std::vector<uint16_t>& indices)
{
    if(indices.empty()) return;

    READWRITE(COMPACTSIZE(static_cast<uint64_t>(indices[0])));

    uint16_t expected = 1;
    for (size_t i = 1; i < indices.size(); i++) {
        uint64_t index = indices[i] - expected;
        READWRITE(COMPACTSIZE(index));
        expected = indices[i] + 1;
    }
}

class BlockTransactionsRequest {
public:
    // A BlockTransactionsRequest message
    uint256 blockhash;
    std::vector<uint16_t> m_transaction_indices;
    std::vector<uint16_t> m_referral_indices;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockhash);

        uint64_t m_transaction_indices_size = static_cast<uint64_t>(m_transaction_indices.size());
        READWRITE(COMPACTSIZE(m_transaction_indices_size));

        uint64_t m_referral_indices_size = static_cast<uint64_t>(m_referral_indices.size());
        READWRITE(COMPACTSIZE(m_referral_indices_size));

        if (ser_action.ForRead()) {
            ReadCompressedIndices(s, ser_action, m_transaction_indices_size, m_transaction_indices);
            ReadCompressedIndices(s, ser_action, m_referral_indices_size, m_referral_indices);
        } else {
            WriteCompressedIndices(s, ser_action, m_transaction_indices);
            WriteCompressedIndices(s, ser_action, m_referral_indices);
        }
    }
};

class BlockTransactions {
public:
    // A BlockTransactions message
    uint256 blockhash;
    std::vector<CTransactionRef> txn;
    std::vector<ReferralRef> refs;

    BlockTransactions() {}
    explicit BlockTransactions(const BlockTransactionsRequest& req) :
        blockhash{req.blockhash}, 
        txn(req.m_transaction_indices.size()),
        refs(req.m_referral_indices.size()) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) 
    {
        READWRITE(blockhash);

        uint64_t txn_size = txn.size();
        uint64_t ref_size = refs.size();

        READWRITE(COMPACTSIZE(txn_size));
        READWRITE(COMPACTSIZE(ref_size));

        if (ser_action.ForRead()) {
            txn.resize(txn_size);
            for(auto& tx : txn) {
                READWRITE(REF(TransactionCompressor(tx)));
            }

            for(auto& ref : refs) {
                READWRITE(REF(ReferralCompressor(ref)));
            }
        } else {
            for(auto& tx : txn) {
                READWRITE(REF(TransactionCompressor(tx)));
            }

            for(auto& ref : refs) {
                READWRITE(REF(ReferralCompressor(ref)));
            }
        }
    }
};

// Dumb serialization/storage-helper for BlockHeaderAndShortIDs and PartiallyDownloadedBlock
template <typename IndexValue, typename CompressorType>
struct Prefilled {
    // Used as an offset since last prefilled tx in BlockHeaderAndShortIDs,
    // as a proper transaction-in-block-index in PartiallyDownloadedBlock
    uint16_t index;
    IndexValue value;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint64_t idx = index;
        READWRITE(COMPACTSIZE(idx));
        if (idx > std::numeric_limits<uint16_t>::max())
            throw std::ios_base::failure("transaction index overflowed 16-bits");
        index = idx;
        READWRITE(REF(CompressorType(value)));
    }
};

using PrefilledTransaction = Prefilled<CTransactionRef, TransactionCompressor>;
using PrefilledReferral = Prefilled<ReferralRef, ReferralCompressor>;

typedef enum ReadStatus_t
{
    READ_STATUS_OK,
    READ_STATUS_INVALID, // Invalid object, peer is sending bogus crap
    READ_STATUS_FAILED, // Failed to process object
    READ_STATUS_CHECKBLOCK_FAILED, // Used only by FillBlock to indicate a
                                   // failure in CheckBlock.
} ReadStatus;

using ShortIds = std::vector<uint64_t>;
const int SHORT_ID_LENGTH = 6;

template<typename Stream, typename Operation>
void ReadShortIds(Stream& s, Operation ser_action, uint64_t size, ShortIds& ids) {
    ids.resize(size);
    for(auto& id : ids) {
        uint32_t lsb = 0; uint16_t msb = 0;
        READWRITE(lsb);
        READWRITE(msb);
        id = (uint64_t(msb) << 32) | uint64_t(lsb);
        static_assert(SHORT_ID_LENGTH == 6, "shorttxids serialization assumes 6-byte shorttxids");
    }
}

template<typename Stream, typename Operation>
void WriteShortIds(Stream& s, Operation ser_action, const ShortIds& ids) {
    for(const auto& id : ids) {
        uint32_t lsb = id & 0xffffffff;
        uint16_t msb = (id >> 32) & 0xffff;
        READWRITE(lsb);
        READWRITE(msb);
    }
}


class BlockHeaderAndShortIDs {
private:
    mutable uint64_t m_short_idk0, m_short_idk1;
    uint64_t m_nonce;

    void FillShortIDSelector() const;

    friend class PartiallyDownloadedBlock;

protected:
    ShortIds m_short_tx_ids;
    ShortIds m_short_ref_ids;
    std::vector<PrefilledTransaction> m_prefilled_txn;

public:
    CBlockHeader header;

    // Dummy for deserialization
    BlockHeaderAndShortIDs() {}

    BlockHeaderAndShortIDs(const CBlock& block, bool fUseWTXID);

    size_t BlockTxCount() const { return m_short_tx_ids.size() + m_prefilled_txn.size(); }
    size_t BlockRefCount() const { return m_short_ref_ids.size(); }

    uint64_t GetShortID(const uint256& hash) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(m_nonce);

        uint64_t short_tx_ids_size = m_short_tx_ids.size();
        READWRITE(COMPACTSIZE(short_tx_ids_size));

        uint64_t short_ref_ids_size = m_short_ref_ids.size();
        READWRITE(COMPACTSIZE(short_ref_ids_size));

        if (ser_action.ForRead()) {
            ReadShortIds(s, ser_action, short_tx_ids_size, m_short_tx_ids);
            ReadShortIds(s, ser_action, short_ref_ids_size, m_short_ref_ids);
        } else {
            WriteShortIds(s, ser_action, m_short_tx_ids);
            WriteShortIds(s, ser_action, m_short_ref_ids);
        }

        READWRITE(m_prefilled_txn);

        if (ser_action.ForRead())
            FillShortIDSelector();
    }
};

using MissingTransactions = std::vector<CTransactionRef>;
using MissingReferrals = std::vector<ReferralRef>;
using ExtraTransactions = std::vector<std::pair<uint256, CTransactionRef>>;
using ExtraReferrals = std::vector<std::pair<uint256, ReferralRef>>;

class PartiallyDownloadedBlock {
protected:
    std::vector<CTransactionRef> m_txn_available;
    std::vector<ReferralRef> m_refs_available;

    size_t m_prefilled_txn_count = 0, m_mempool_txn_count = 0, m_extra_txn_count = 0;
    size_t m_mempool_ref_count = 0, m_extra_ref_count = 0;

    CTxMemPool* m_txn_pool;
    ReferralTxMemPool* m_ref_pool;

public:
    CBlockHeader header;
    explicit PartiallyDownloadedBlock(CTxMemPool* txn_pool_in, ReferralTxMemPool* ref_pool_in) : 
        m_txn_pool{txn_pool_in}, m_ref_pool{ref_pool_in} 
    {
        assert(m_txn_pool);
        assert(m_ref_pool);
    }

    // extra_txn is a list of extra transactions to look at, in <witness hash, reference> form
    ReadStatus InitData(const BlockHeaderAndShortIDs& cmpctblock, 
            const ExtraTransactions& extra_txn,
            const ExtraReferrals& extra_ref);

    bool IsTxAvailable(size_t index) const;
    bool IsRefAvailable(size_t index) const;
    ReadStatus FillBlock(CBlock& block, 
            const MissingTransactions& vtx_missing, 
            const MissingReferrals& ref_missing);
};

#endif
