// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refmempool.h"

#include "consensus/validation.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "reverse_iterator.h"
#include "streams.h"
#include "validation.h"

#include <algorithm>
#include <numeric>
#include <deque>

namespace referral
{

const Address& GetAddress(const RefMemPoolEntry& entry)
{
    return entry.GetSharedEntryValue()->GetAddress();
}

const std::string& GetAlias(const RefMemPoolEntry& entry)
{
    return entry.GetSharedEntryValue()->alias;
}

const Address& GetParentAddress(const RefMemPoolEntry& entry)
{
    return entry.GetSharedEntryValue()->parentAddress;
}

RefMemPoolEntry::RefMemPoolEntry(const Referral& _entry, int64_t _nTime, unsigned int _entryHeight) : MemPoolEntry(_entry, _nTime, _entryHeight)
{
    nWeight = GetReferralWeight(_entry);
    nUsageSize = RecursiveDynamicUsage(entry);
    nCountWithDescendants = 1;
}

size_t RefMemPoolEntry::GetSize() const
{
    return GetVirtualReferralSize(nWeight);
}

void RefMemPoolEntry::UpdateDescendantsCount(int64_t modifyCount)
{
    nCountWithDescendants += modifyCount;
    assert(int64_t(nCountWithDescendants) > 0);
}

bool ReferralTxMemPool::AddUnchecked(const uint256& hash, const RefMemPoolEntry& entry)
{
    NotifyEntryAdded(entry.GetSharedEntryValue());

    LOCK(cs);

    RefIter newit = mapRTx.insert(entry).first;
    mapChildren.insert(std::make_pair(newit, setEntries()));

    // check mempool referrals for a parent
    auto parentit = mapRTx.get<referral_address>().find(entry.GetEntryValue().parentAddress);

    if (parentit != mapRTx.get<referral_address>().end()) {
        mapChildren[mapRTx.project<0>(parentit)].insert(newit);
        mapRTx.modify(mapRTx.project<0>(parentit), update_descendants_count(1));

        setEntries s;
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    }

    cachedInnerUsage += entry.DynamicMemoryUsage();
    assert(cachedInnerUsage > 0);

    return true;
}

void ReferralTxMemPool::CalculateDescendants(RefIter entryit, setEntries& setDescendants) const
{
    setEntries stage;
    if (setDescendants.count(entryit) == 0) {
        stage.insert(entryit);
    }

    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have either
    // already been walked, or will be walked in this iteration).
    while (!stage.empty()) {
        RefIter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it);

        const setEntries& setChildren = GetMemPoolChildren(it);
        for (const RefIter& childit : setChildren) {
            if (!setDescendants.count(childit)) {
                stage.insert(childit);
            }
        }
    }
}

const ReferralTxMemPool::setEntries& ReferralTxMemPool::GetMemPoolChildren(RefIter entryit) const
{
    assert(entryit != mapRTx.end());
    RefLinksMap::const_iterator it = mapChildren.find(entryit);
    assert(it != mapChildren.end());
    return it->second;
}

void ReferralTxMemPool::RemoveRecursive(const Referral& origRef, MemPoolRemovalReason reason)
{
    // Remove referrals from memory pool
    LOCK(cs);

    RefIter origit = mapRTx.find(origRef.GetHash());

    if (origit != mapRTx.end()) {
        setEntries toRemove;

        CalculateDescendants(origit, toRemove);
        RemoveStaged(toRemove, reason);
    }
}

void ReferralTxMemPool::RemoveForBlock(const std::vector<ReferralRef>& vRefs)
{
    LOCK(cs);

    for (const auto& ref : vRefs) {
        auto it = mapRTx.find(ref->GetHash());

        if (it != mapRTx.end()) {
            RemoveUnchecked(it, MemPoolRemovalReason::BLOCK);
        }
    }
}

void ReferralTxMemPool::RemoveUnchecked(RefIter it, MemPoolRemovalReason reason)
{
    NotifyEntryRemoved(it->GetSharedEntryValue(), reason);

    // check mempool referrals for a parent
    auto parentit = mapRTx.get<referral_address>().find(it->GetEntryValue().parentAddress);

    if (parentit != mapRTx.get<referral_address>().end()) {
        mapChildren[mapRTx.project<0>(parentit)].erase(it);
        mapRTx.modify(mapRTx.project<0>(parentit), update_descendants_count(-1));

        setEntries s;
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }

    assert(mapChildren.count(it));

    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapChildren[it]);

    mapChildren.erase(it);
    mapRTx.erase(it);

    assert(cachedInnerUsage >= 0);
}

void ReferralTxMemPool::RemoveStaged(setEntries& stage, MemPoolRemovalReason reason)
{
    AssertLockHeld(cs);
    // UpdateForRemoveFromMempool(stage, updateDescendants);
    for (const RefIter& it : stage) {
        RemoveUnchecked(it, reason);
    }
}

void ReferralTxMemPool::TrimToSize(size_t limit) {
    LOCK(cs);

    CFeeRate maxFeeRateRemoved(0);
    while (!mapRTx.empty() && DynamicMemoryUsage() > limit) {
        auto it = mapRTx.get<descendants_count>().begin();

        RemoveRecursive(it->GetEntryValue(), MemPoolRemovalReason::SIZELIMIT);
    }
}

int ReferralTxMemPool::Expire(int64_t time) {
    LOCK(cs);

    auto it = mapRTx.get<entry_time>().begin();
    setEntries toRemove;
    while (it != mapRTx.get<entry_time>().end() && it->GetTime() < time) {
        toRemove.insert(mapRTx.project<0>(it));
        it++;
    }
    setEntries stage;
    for (RefIter removeit : toRemove) {
        CalculateDescendants(removeit, stage);
    }
    RemoveStaged(stage, MemPoolRemovalReason::EXPIRY);

    return stage.size();
}

namespace {
    class ReferralIdVisitor : public boost::static_visitor<ReferralRefs>
    {
    private:
        const ReferralTxMemPool *mempool;
    public:
        explicit ReferralIdVisitor(const ReferralTxMemPool *mempool_in): mempool{mempool_in} {}

        ReferralRefs operator()(const std::string &id) const {
            return mempool->Get(id);
        }

        template <typename T>
        ReferralRefs operator()(const T &id) const {
            return {mempool->Get(id)};
        }
    };
}

ReferralRef ReferralTxMemPool::Get(const uint256& hash) const
{
    LOCK(cs);
    auto it = mapRTx.find(hash);
    return it != mapRTx.end() ? it->GetSharedEntryValue() : nullptr;
}

ReferralRef ReferralTxMemPool::Get(const Address& address) const
{
    LOCK(cs);
    auto it = mapRTx.get<referral_address>().find(address);
    return it != mapRTx.get<referral_address>().end() ? it->GetSharedEntryValue() : nullptr;
}

ReferralRefs ReferralTxMemPool::Get(const std::string& alias) const
{
    LOCK(cs);
    auto range = mapRTx.get<referral_alias>().equal_range(alias);
    ReferralRefs refs;
    for(auto it = range.first ; it != range.second; it++) {
        refs.push_back(it->GetSharedEntryValue());
    }
    return refs;
}

ReferralRefs ReferralTxMemPool::Get(const ReferralId& referral_id) const
{
    return boost::apply_visitor(ReferralIdVisitor(this), referral_id);
}

std::pair<ReferralTxMemPool::RefAliasIter, ReferralTxMemPool::RefAliasIter> ReferralTxMemPool::Find(const std::string& alias) const
{
    return mapRTx.get<referral_alias>().equal_range(alias);
}

std::pair<ReferralTxMemPool::RefParentIter, ReferralTxMemPool::RefParentIter> ReferralTxMemPool::Find(const Address& parentAddress) const
{
    return mapRTx.get<referral_parent>().equal_range(parentAddress);
}

bool ReferralTxMemPool::Exists(const uint256& hash) const
{
    LOCK(cs);
    return mapRTx.count(hash) != 0;
}

bool ReferralTxMemPool::Exists(const Address& address) const
{
    return mapRTx.get<referral_address>().count(address) != 0;
}

bool ReferralTxMemPool::Exists(const std::string& alias) const
{
    LOCK(cs);
    return alias.size() > 0 && mapRTx.get<referral_alias>().count(alias) != 0;
}

void ReferralTxMemPool::GetReferralsForTransaction(const CTransactionRef& tx, ReferralTxMemPool::setEntries& txReferrals)
{
    std::deque<RefAddressIter> queue;
    // check addresses used for vouts are beaconed
    for (const auto& txout : tx->vout) {
        CTxDestination dest;
        uint160 addr;
        // CNoDestination script
        if (!ExtractDestination(txout.scriptPubKey, dest)) {
            continue;
        }

        assert(GetUint160(dest, addr));

        bool addressBeaconed = prefviewcache->Exists(addr);

        // check cache for beaconed address
        if (addressBeaconed) {
            continue;
        }

        // check mempoolReferral for beaconed address
        auto it = mapRTx.get<referral_address>().find(addr);

        if (it != mapRTx.get<referral_address>().end()) {
            queue.push_back(it);
        }
    }

    while(!queue.empty())
    {
        auto it = queue.front();
        txReferrals.insert(mapRTx.project<0>(it));

        queue.pop_front();

        //Find and add parent
        auto ref = it->GetSharedEntryValue();
        auto parent_it = mapRTx.get<referral_address>().find(ref->parentAddress);
        if(parent_it != mapRTx.get<referral_address>().end()) {
            queue.push_back(parent_it);
        }
    }
}

std::vector<ReferralRef> ReferralTxMemPool::GetReferrals() const
{
    LOCK(cs);

    std::vector<ReferralRef> refs(mapRTx.size());

    std::transform(mapRTx.begin(), mapRTx.end(), refs.begin(),
            [](const RefMemPoolEntry& entry) {
                return entry.GetSharedEntryValue();
            });

    return refs;
}

size_t ReferralTxMemPool::DynamicMemoryUsage() const
{
    LOCK(cs);
    return memusage::MallocUsage(sizeof(RefMemPoolEntry) + 15 * sizeof(void*)) * mapRTx.size() + memusage::DynamicUsage(mapChildren) + cachedInnerUsage;
}


void ReferralTxMemPool::Clear()
{
    LOCK(cs);
    mapChildren.clear();
    mapRTx.clear();
    cachedInnerUsage = 0;
}
}
