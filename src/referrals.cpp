// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

ReferralsView::ReferralsView(ReferralsViewDB *db) : m_db{db} {
    assert(db);
};

ReferralMap::iterator ReferralsView::Fetch(const uint256& code) const {
    return m_referral_cache.find(code);
}

bool ReferralsView::GetReferral(const uint256& code, MutableReferral& ref) const {
    {
        LOCK(m_cs_cache);
        auto it = Fetch(code);
        if (it != m_referral_cache.end()) {
            ref = it->second;
            return true;
        }
    }
    if (m_db->GetReferral(code, ref)) {
        LOCK(m_cs_cache);
        auto ret = m_referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
        return ret.second;
    }
    return false;
}

bool ReferralsView::InsertReferral(const Referral& ref) {
    LOCK(m_cs_cache);
    auto ret = m_referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
    return ret.second;
}

bool ReferralsView::ReferralCodeExists(const uint256& code) const {
    LOCK(m_cs_cache);
    return m_referral_cache.count(code) > 0;
}

void ReferralsView::Flush() {
    LOCK(m_cs_cache);
    // todo: use batch insert
    for (auto pr : m_referral_cache) {
        m_db->InsertReferral(pr.second);
    }
    m_referral_cache.clear();
}

