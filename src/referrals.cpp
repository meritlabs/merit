// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

namespace referral
{

ReferralsViewCache::ReferralsViewCache(ReferralsViewDB *db) : m_db{db}
{
    assert(db);
};

ReferralMap::iterator ReferralsViewCache::Fetch(const uint256& code) const
{
    return m_referral_cache.find(code);
}

MaybeReferral ReferralsViewCache::GetReferral(const uint256& code) const
{
    {
        LOCK(m_cs_cache);
        auto it = Fetch(code);
        if (it != m_referral_cache.end()) {
            return it->second;
        }
    }

    if (auto ref = m_db->GetReferral(code)) {
        InsertReferralIntoCache(*ref);
        return ref;
    }
    return {};
}

void ReferralsViewCache::InsertReferralIntoCache(const Referral& ref) const
{
    LOCK(m_cs_cache);
    //insert into referral cache
    m_referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
}

bool ReferralsViewCache::ReferralCodeExists(const uint256& code) const
{
    {
        LOCK(m_cs_cache);
        if (m_referral_cache.count(code) > 0) {
            return true;
        }
    }
    if (auto ref = m_db->GetReferral(code)) {
        InsertReferralIntoCache(*ref);
        return true;
    }
    return false;
}

void ReferralsViewCache::Flush()
{
    LOCK(m_cs_cache);
    // todo: use batch insert
    for (auto pr : m_referral_cache) {
        m_db->InsertReferral(pr.second);
    }
    m_referral_cache.clear();
}

void ReferralsViewCache::InsertWalletRelationshipIntoCache(const Address& child, const Address& parent) const
{
    LOCK(m_cs_cache);
    m_wallet_to_referrer.insert(std::make_pair(child, parent));
}

MaybeAddress ReferralsViewCache::GetReferrer(const Address& address) const
{
    {
        LOCK(m_cs_cache);
        auto it = m_wallet_to_referrer.find(address);
        if (it != std::end(m_wallet_to_referrer)) {
            return it->second;
        }
    }

    if (auto parent = m_db->GetReferrer(address)) {
        InsertWalletRelationshipIntoCache(address, *parent);
        return parent;
    }
    return {};
}

// TODO: Consider naming here.
bool ReferralsViewCache::WalletIdExists(const Address& address) const
{
    {
        LOCK(m_cs_cache);
        auto it = m_wallet_to_referrer.find(address);
        if (it != std::end(m_wallet_to_referrer)) {
            return true;
        }
    }
    if (auto parent = m_db->GetReferrer(address)) {
        InsertWalletRelationshipIntoCache(address, *parent);
        return true;
    }
    return false;
}

void ReferralsViewCache::RemoveReferral(const Referral& ref) const
{
    m_referral_cache.erase(ref.m_codeHash);
    m_db->RemoveReferral(ref);
}
}
