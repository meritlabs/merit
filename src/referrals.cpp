// Copyright (c) 2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

namespace referral
{
ReferralsViewCache::ReferralsViewCache(ReferralsViewDB* db) : m_db{db}
{
    assert(db);
};

MaybeReferral ReferralsViewCache::GetReferral(const Address& address) const
{
    {
        LOCK(m_cs_cache);
        auto it = referrals_index.find(address);
        if (it != referrals_index.end()) {
            return *it;
        }
    }

    if (auto ref = m_db->GetReferral(address)) {
        InsertReferralIntoCache(*ref);
        return ref;
    }

    return {};
}

bool ReferralsViewCache::Exists(const uint256& hash) const
{
    {
        LOCK(m_cs_cache);
        if (referrals_index.get<by_hash>().count(hash) > 0) {
            return true;
        }
    }

    if (auto ref = m_db->GetReferral(hash)) {
        InsertReferralIntoCache(*ref);
        return true;
    }

    return false;
}

bool ReferralsViewCache::Exists(const Address& address) const
{
    {
        LOCK(m_cs_cache);
        if (referrals_index.count(address) > 0) {
            return true;
        }
    }
    if (auto ref = m_db->GetReferral(address)) {
        InsertReferralIntoCache(*ref);
        return true;
    }
    return false;
}

bool ReferralsViewCache::Exists(
        const std::string& alias,
        bool normalize_alias) const
{
    auto maybe_normalized = alias;
    if(normalize_alias) {
        NormalizeAlias(maybe_normalized);
    }

    if (alias.size() == 0) {
        return false;
    }

    {
        LOCK(m_cs_cache);
        if (alias_index.count(maybe_normalized) > 0) {
            return true;
        }
    }

    if (auto ref = m_db->GetReferral(maybe_normalized, normalize_alias)) {
        LOCK(m_cs_cache);
        referrals_index.insert(*ref);
        alias_index[maybe_normalized] = ref->GetAddress();
        return true;
    }
    return false;
}

void ReferralsViewCache::InsertReferralIntoCache(const Referral& ref) const
{
    LOCK(m_cs_cache);
    referrals_index.insert(ref);
}

void ReferralsViewCache::RemoveAliasFromCache(const Referral& ref) const {
    auto normalized_alias = ref.alias;
    NormalizeAlias(normalized_alias);
    if(alias_index.erase(normalized_alias) == 0) {
        alias_index.erase(ref.alias);
    }
}

bool ReferralsViewCache::RemoveReferral(const Referral& ref) const
{
    referrals_index.erase(ref.GetAddress());
    RemoveAliasFromCache(ref);
   
    return m_db->RemoveReferral(ref);
}

bool ReferralsViewCache::UpdateConfirmation(char address_type, const Address& address, CAmount amount)
{
    assert(m_db);
    CAmount updated_amount;
    //TODO: Have an in memory cache. For now just passthrough.
    if (!m_db->UpdateConfirmation(address_type, address, amount, updated_amount)) {
        return false;
    }

    // if referral was unconfirmed, remove it from the cache
    if (updated_amount == 0) {
        auto ref = GetReferral(address);
        if(!ref) {
            return false;
        }

        RemoveAliasFromCache(*ref);
    }

    return true;
}


bool ReferralsViewCache::IsConfirmed(const Address& address) const
{
    assert(m_db);
    //TODO: Have an in memory cache. For now just passthrough.
    return m_db->IsConfirmed(address);
}

bool ReferralsViewCache::IsConfirmed(const std::string& alias, bool normalize_alias) const
{
    assert(m_db);
    //TODO: Have an in memory cache. For now just passthrough.
    return m_db->IsConfirmed(alias, normalize_alias);
}

}
