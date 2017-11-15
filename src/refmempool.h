// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_REFMEMPOOL_H
#define MERIT_REFMEMPOOL_H

#include "primitives/referral.h"
#include "txmempool.h"

#include <set>
#include <vector>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/signals2/signal.hpp>

namespace referral
{
class RefMemPoolEntry : public MemPoolEntry<Referral>
{
public:
    RefMemPoolEntry(const Referral& _ref, int64_t _nTime, unsigned int _entryHeight);

    using MemPoolEntry::MemPoolEntry;
    size_t GetSize() const;
};

class ReferralTxMemPool
{
public:
    using indexed_referrals_set = boost::multi_index_container<
        RefMemPoolEntry,
        boost::multi_index::indexed_by<
            // sorted by hash
            boost::multi_index::hashed_unique<
                mempoolentry_id<Referral>,
                SaltedTxidHasher>,
            // sorted by entry time
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<entry_time>,
                boost::multi_index::identity<RefMemPoolEntry>,
                CompareMemPoolEntryByEntryTime<Referral>>>>;

    using refiter = indexed_referrals_set::nth_index<0>::type::iterator;

    using setEntries = std::set<refiter, CompareIteratorByHash<refiter>>;

    mutable CCriticalSection cs;

    indexed_referrals_set mapRTx;

    unsigned int nReferralsUpdated;

    ReferralTxMemPool() : nReferralsUpdated(0) {};

    /**
     * Add RefMemPoolEntry to mempool
     * Adds an entry to the map and updates state of children
     */
    bool AddUnchecked(const uint256& hash, const RefMemPoolEntry& entry);

    /**
     * Called when a block is disconnected.
     * Removes set of referrals with all it's descendants from mempool.
     */
    void RemoveRecursive(const Referral &origRef, MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);
    // void removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags);

    /**
     * Called when a block is connected.
     * Removes referrals from mempool.
     */
    void RemoveForBlock(const std::vector<ReferralRef>& vRefs);

    /**
     *  Remove referral from the mempool
     */
    void RemoveUnchecked(refiter it, MemPoolRemovalReason reason);

    /**
     *  Remove a set of referrals from the mempool.
     *  If a referral is in this set, then all in-mempool descendants must
     *  also be in the set, unless this referral is being removed for being in a block.
     */
    void RemoveStaged(setEntries &stage, MemPoolRemovalReason reason);

    /**
     * Expire all transaction (and their dependencies) in the mempool older than time.
     * Return the number of removed transactions.
     */
    int Expire(int64_t time);

    /**
     *  Populate setDescendants with all in-mempool descendants of referral.
     *  Assumes that setDescendants includes all in-mempool descendants of anything
     *  already in it.
     */
    void CalculateDescendants(refiter entryit, setEntries& setDescendants) const;

    /**
     * Get children of a given referral
     */
    const setEntries& GetMemPoolChildren(refiter entry) const;


    /**
     *  Check if referral with a given code hash (hash of unlock code) exists in mempool
     */
    bool ExistsWithCodeHash(const uint256& hash) const;

    /**
     *  Get referral with a given code hash (hash of unlock code) from mempool
     */
    ReferralRef GetWithCodeHash(const uint256& codeHash) const;

    /**
     * Check if referral with a given hash exists in mempoll
     *
     * TODO: update referral model to use one hash for referral id and unlock code
     */
    bool exists(const uint256& hash) const
    {
        LOCK(cs);
        return (mapRTx.count(hash) != 0);
    }

    ReferralRef get(const uint256& hash) const;

    unsigned long size()
    {
        LOCK(cs);
        return mapRTx.size();
    }

    std::vector<ReferralRef> GetReferrals() const;

    size_t DynamicMemoryUsage() const;

    void Clear();

    boost::signals2::signal<void (ReferralRef)> NotifyEntryAdded;
    boost::signals2::signal<void (ReferralRef, MemPoolRemovalReason)> NotifyEntryRemoved;

private:
    struct RefLinks {
        setEntries parents;
        setEntries children;
    };

    using reflinksMap = std::map<refiter, RefLinks, CompareIteratorByHash<refiter>>;
    reflinksMap mapLinks;
};
}

#endif // MERIT_REFMEMPOOL_H
