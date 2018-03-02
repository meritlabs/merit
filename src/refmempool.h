// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_REFMEMPOOL_H
#define MERIT_REFMEMPOOL_H

#include "mempool.h"
#include "primitives/referral.h"
#include "primitives/transaction.h"
#include "referrals.h"
#include "sync.h"

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
private:
    uint64_t nCountWithDescendants;

public:
    RefMemPoolEntry(const Referral& _ref, int64_t _nTime, unsigned int _entryHeight);

    // Adjusts the descendants state.
    void UpdateDescendantsCount(int64_t modifyCount);

    uint64_t GetCountWithDescendants() const { return nCountWithDescendants; }

    size_t GetSize() const;
};

struct descendants_count {
};

// Helpers for modifying ReferralMemPool::mapRTx, which is a boost multi_index.
struct update_descendants_count {
    update_descendants_count(int64_t _modifyCount) : modifyCount(_modifyCount) {}

    void operator()(RefMemPoolEntry& e) { e.UpdateDescendantsCount(modifyCount); }

private:
    int64_t modifyCount;
};

/**
 * Sort by number of descendants in ascendent order
 * or by entry time in descendant order
 */
class CompareRefMemPoolEntryByDescendantsCount
{
public:
    bool operator()(const RefMemPoolEntry& a, const RefMemPoolEntry& b) const
    {
        double f1 = (double)a.GetCountWithDescendants();
        double f2 = (double)b.GetCountWithDescendants();

        if (f1 == f2) {
            return a.GetTime() > b.GetTime();
        }
        return f1 < f2;
    }
};

struct referral_address {
};

struct referral_alias {
};

struct referral_parent {
};

const Address& GetAddress(const RefMemPoolEntry& entry);
const std::string& GetAlias(const RefMemPoolEntry& entry);
const Address& GetParentAddress(const RefMemPoolEntry& entry);

class ReferralTxMemPool
{
private:
    // sum of dynamic memory usage of all the map elements (NOT the maps themselves)
    uint64_t cachedInnerUsage;

public:
    using indexed_referrals_set = boost::multi_index_container<
        RefMemPoolEntry,
        boost::multi_index::indexed_by<
            // sorted by hash
            boost::multi_index::hashed_unique<
                MemPoolEntryHash<Referral>,
                SaltedTxidHasher>,
                // sorted by address
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<referral_address>,
                boost::multi_index::global_fun<
                    const RefMemPoolEntry&,
                    const Address&,
                    &GetAddress>,
                SaltedHasher<160>>,
            // use non-unique here to support empty tags.
            // otherwise it won't add such referrals to index
            // uniqueness is provided by validation
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<referral_alias>,
                boost::multi_index::global_fun<
                    const RefMemPoolEntry&,
                    const std::string&,
                    &GetAlias>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<referral_parent>,
                boost::multi_index::global_fun<
                    const RefMemPoolEntry&,
                    const Address&,
                    &GetParentAddress>,
                SaltedHasher<160>>,
            // sorted by descendants count
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<descendants_count>,
                boost::multi_index::identity<RefMemPoolEntry>,
                CompareRefMemPoolEntryByDescendantsCount>,
            // sorted by entry time
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<entry_time>,
                boost::multi_index::identity<RefMemPoolEntry>,
                CompareMemPoolEntryByEntryTime<Referral>>>>;

    using RefIter = indexed_referrals_set::nth_index<0>::type::iterator;
    using RefAddressIter = indexed_referrals_set::nth_index<1>::type::iterator;
    using RefAliasIter = indexed_referrals_set::nth_index<2>::type::iterator;
    using RefParentIter = indexed_referrals_set::nth_index<3>::type::iterator;

    using setEntries = std::set<RefIter, CompareIteratorByHash<RefIter>>;

    mutable CCriticalSection cs;

    indexed_referrals_set mapRTx;

    ReferralTxMemPool(){};

    /**
     * Add RefMemPoolEntry to mempool
     * Adds an entry to the map and updates state of children
     */
    bool AddUnchecked(const uint256& hash, const RefMemPoolEntry& entry);

    /**
     * Called when a block is disconnected.
     * Removes set of referrals with all it's descendants from mempool.
     */
    void RemoveRecursive(const Referral& origRef, MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);
    // void removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags);

    /**
     * Called when a block is connected.
     * Removes referrals from mempool.
     */
    void RemoveForBlock(const std::vector<ReferralRef>& vRefs);

    /**
     *  Remove referral from the mempool
     */
    void RemoveUnchecked(RefIter it, MemPoolRemovalReason reason);

    /**
     *  Remove a set of referrals from the mempool.
     *  If a referral is in this set, then all in-mempool descendants must
     *  also be in the set, unless this referral is being removed for being in a block.
     */
    void RemoveStaged(setEntries& stage, MemPoolRemovalReason reason);

    /**
     * Remove referrals from the mempool until its size is <= sizelimit.
     */
    void TrimToSize(size_t limit);

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
    void CalculateDescendants(RefIter entryit, setEntries& setDescendants) const;

    /**
     * Get children of a given mempool entry referral
     */
    const setEntries& GetMemPoolChildren(RefIter entry) const;

    /**
     * Check if referral with a given hash exists in mempoll
     */
    bool Exists(const uint256& hash) const;

    /**
     *  Check if referral with a given address exists in mempool
     */
    bool Exists(const Address& address) const;

    /**
     * Check if referral with a given alias exists in mempoll
     */
    bool Exists(const std::string& alias) const;

    /** Get referral by hash */
    ReferralRef Get(const uint256& hash) const;

    /** Get referral by address */
    ReferralRef Get(const Address& address) const;

    /** Get referral by alias */
    ReferralRef Get(const std::string& alias) const;

    /** Get referral by id - hash, address or alias */
    ReferralRef Get(const ReferralId& referral_id) const;

    /** Find all referrals with given alias */
    std::pair<RefAliasIter, RefAliasIter> Find(const std::string& alias) const;

    /** Find all referrals with given parent address */
    std::pair<RefParentIter, RefParentIter> Find(const Address& parentAddress) const;

    unsigned long Size() const
    {
        LOCK(cs);
        return mapRTx.size();
    }

    std::vector<ReferralRef> GetReferrals() const;

    /**
     * Get set of referrals that given transaction depends on
     */
    void GetReferralsForTransaction(const CTransactionRef& tx, referral::ReferralTxMemPool::setEntries& txReferrals);

    size_t DynamicMemoryUsage() const;

    void Clear();

    boost::signals2::signal<void(ReferralRef)> NotifyEntryAdded;
    boost::signals2::signal<void(ReferralRef, MemPoolRemovalReason)> NotifyEntryRemoved;

private:
    using RefLinksMap = std::map<RefIter, setEntries, CompareIteratorByHash<RefIter>>;
    RefLinksMap mapChildren;
};
}

#endif // MERIT_REFMEMPOOL_H
