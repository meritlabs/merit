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

    bool AddUnchecked(const uint256& hash, const RefMemPoolEntry& entry);

    void RemoveRecursive(const Referral &origRef, MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);
    // void removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags);
    void RemoveForBlock(const std::vector<ReferralRef>& vRefs);
    void RemoveStaged(const Referral& ref, MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);

    void CalculateDescendants(refiter entryit, setEntries& setDescendants);

    bool exists(const uint256& hash) const
    {
        LOCK(cs);
        return (mapRTx.count(hash) != 0);
    }

    bool ExistsWithCodeHash(const uint256& hash) const;
    ReferralRef GetWithCodeHash(const uint256& codeHash) const;

    ReferralRef get(const uint256& hash) const;

    unsigned long size()
    {
        LOCK(cs);
        return mapRTx.size();
    }

    std::vector<ReferralRef> GetReferrals() const;

    size_t DynamicMemoryUsage() const;

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
