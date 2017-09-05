// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

ReferralsViewCache::ReferralsViewCache(ReferralsViewDB *db) : m_db{db} {
    assert(db);
};

ReferralMap::iterator ReferralsViewCache::Fetch(const uint256& code) const {
    return m_referral_cache.find(code);
}

bool ReferralsViewCache::GetReferral(const uint256& code, MutableReferral& ref) const {
    {
        LOCK(m_cs_cache);
        auto it = Fetch(code);
        if (it != m_referral_cache.end()) {
            ref = it->second;
            return true;
        }
    }
    if (m_db->GetReferral(code, ref)) {
        return InsertReferralIntoCache(ref);
    }
    return false;
}

bool ReferralsViewCache::InsertReferralIntoCache(const Referral& ref) const {
    LOCK(m_cs_cache);
    auto ret = m_referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
    return ret.second;
}

bool ReferralsViewCache::ReferralCodeExists(const uint256& code) const {
    {
        LOCK(m_cs_cache);
        if (m_referral_cache.count(code) > 0) {
            return true;
        }
    }
    MutableReferral db_ref;
    m_db->ListKeys();
    if (m_db->GetReferral(code, db_ref)) {
        return InsertReferralIntoCache(db_ref);
    }
    return false;
}

void ReferralsViewCache::Flush() {
    LOCK(m_cs_cache);
    // todo: use batch insert
    for (auto pr : m_referral_cache) {
        m_db->InsertReferral(pr.second);
    }
    m_referral_cache.clear();
}

