
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_MEMPOOL_H
#define MERIT_MEMPOOL_H

#include <memory>

#include "addressindex.h"
#include "hash.h"
#include "memusage.h"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

/**
 * This file contains shared entities for mempool and refmempool
 */

/**
 * Reason why a transaction was removed from the mempool,
 * this is passed to the notification signal.
 */
enum class MemPoolRemovalReason {
    UNKNOWN = 0, //! Manually removed or unknown reason
    EXPIRY,      //! Expired from mempool
    SIZELIMIT,   //! Removed in size limiting
    REORG,       //! Removed for reorganization
    BLOCK,       //! Removed for block
    CONFLICT,    //! Removed for conflict with in-block transaction
    REPLACED     //! Removed for replacement
};

template <typename T>
class MemPoolEntry
{
protected:
    std::shared_ptr<const T> entry;
    size_t nWeight;           //!< ... and avoid recomputing referral weight (also used for GetSize())
    size_t nUsageSize;        //!< ... and total memory usage
    int64_t nTime;            //!< Local time when entering the mempool
    unsigned int entryHeight; //!< Chain height when entering the mempool

public:
    MemPoolEntry(const T& _entry, int64_t _nTime, unsigned int _entryHeight) : entry(std::make_shared<const T>(_entry)),
                                                                               nTime(_nTime),
                                                                               entryHeight(_entryHeight) { }

    const T& GetEntryValue() const { assert(entry); return *entry; }
    const std::shared_ptr<const T> GetSharedEntryValue() const { return entry; }

    virtual size_t GetSize() const = 0;

    size_t GetWeight() const { return nWeight; }
    int64_t GetTime() const { return nTime; }
    unsigned int GetHeight() const { return entryHeight; }
    size_t DynamicMemoryUsage() const { return nUsageSize; }
};

class SaltedTxidHasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedTxidHasher();

    size_t operator()(const uint256& txid) const
    {
        return SipHashUint256(k0, k1, txid);
    }
};

// extracts a entry hash
template <typename T>
struct MemPoolEntryHash {
    typedef uint256 result_type;
    result_type operator()(const MemPoolEntry<T>& entry) const
    {
        return entry.GetEntryValue().GetHash();
    }

    result_type operator()(const std::shared_ptr<const T>& entry) const
    {
        return entry->GetHash();
    }
};

// multi_index comparators
template <typename iter>
struct CompareIteratorByHash {
    bool operator()(const iter& a, const iter& b) const
    {
        assert(a->GetSharedEntryValue());
        assert(b->GetSharedEntryValue());
        return a->GetSharedEntryValue()->GetHash() < b->GetSharedEntryValue()->GetHash();
    }
};
template <typename iter>
struct CompareIteratorByEntryTime {
    bool operator()(const iter& a, const iter& b) const
    {
        return a->GetTime() < b->GetTime();
    }
};

template <typename T>
class CompareMemPoolEntryByEntryTime
{
public:
    bool operator()(const MemPoolEntry<T>& a, const MemPoolEntry<T>& b) const
    {
        return a.GetTime() < b.GetTime();
    }
};

// Multi_index tag names
struct entry_time {};

/**
 * DisconnectedBlockEntries

 * During the reorg, it's desirable to re-add previously confirmed transactions
 * to the mempool, so that anything not re-confirmed in the new chain is
 * available to be mined. However, it's more efficient to wait until the reorg
 * is complete and process all still-unconfirmed transactions at that time,
 * since we expect most confirmed transactions to (typically) still be
 * confirmed in the new chain, and re-accepting to the memory pool is expensive
 * (and therefore better to not do in the middle of reorg-processing).
 * Instead, store the disconnected transactions (in order!) as we go, remove any
 * that are included in blocks in the new chain, and then process the remaining
 * still-unconfirmed transactions at the end.
 */

// multi_index tag names
struct id_index {};
struct insertion_order {};

template <typename T>
using indexed_disconnected_entries = boost::multi_index_container<
    std::shared_ptr<const T>,
    boost::multi_index::indexed_by<
        // sorted by txid
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<id_index>,
            MemPoolEntryHash<T>,
            SaltedTxidHasher
        >,
        // sorted by order in the blockchain
        boost::multi_index::sequenced<
            boost::multi_index::tag<insertion_order>
        >
    >
>;

template <typename T>
struct DisconnectedBlockEntries {
    using EntryRef = std::shared_ptr<const T>;
    using indexed_disconnected = indexed_disconnected_entries<T>;

    // It's almost certainly a logic bug if we don't clear out queued before
    // destruction, as we add to it while disconnecting blocks, and then we
    // need to re-process remaining transactions to ensure mempool consistency.
    // For now, assert() that we've emptied out this object on destruction.
    // This assert() can always be removed if the reorg-processing code were
    // to be refactored such that this assumption is no longer true (for
    // instance if there was some other way we cleaned up the mempool after a
    // reorg, besides draining this object).
    ~DisconnectedBlockEntries() { assert(queued.empty()); }

    indexed_disconnected queued;
    uint64_t cachedInnerUsage = 0;

    // Estimate the overhead of queued to be 6 pointers + an allocation, as
    // no exact formula for boost::multi_index_contained is implemented.
    size_t DynamicMemoryUsage() const {
        return memusage::MallocUsage(sizeof(EntryRef) + 6 * sizeof(void*)) * queued.size() + cachedInnerUsage;
    }

    void addEntry(const EntryRef& entry)
    {
        queued.insert(entry);
        cachedInnerUsage += RecursiveDynamicUsage(entry);
    }

    // Remove entries based on id_index, and update memory usage.
    void removeForBlock(const std::vector<EntryRef>& entries)
    {
        // Short-circuit in the common case of a block being added to the tip
        if (queued.empty()) {
            return;
        }
        for (auto const &entry : entries) {
            auto it = queued.find(entry->GetHash());
            if (it != queued.end()) {
                cachedInnerUsage -= RecursiveDynamicUsage(*it);
                queued.erase(it);
            }
        }
    }

    // Remove an entry by insertion_order index, and update memory usage.
    void removeEntry(typename indexed_disconnected::template index<insertion_order>::type::iterator entry)
    {
        cachedInnerUsage -= RecursiveDynamicUsage(*entry);
        queued.template get<insertion_order>().erase(entry);
    }

    void clear()
    {
        cachedInnerUsage = 0;
        queued.clear();
    }
};

#endif // MERIT_MEMPOOL_H
