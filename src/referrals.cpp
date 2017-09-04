// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

ReferralMap::iterator ReferralsView::Fetch(const uint256& code) const {
    return m_referral_cache.find(code);
}

bool ReferralsView::GetReferral(const uint256& code, MutableReferral& m_ref) const {
    auto it = Fetch(code);
    if (it != m_referral_cache.end()) {
        m_ref = it->second;
        return true;
    }
    return false;
}

bool ReferralsView::InsertReferral(const Referral& ref) {
    auto ret = m_referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
    return ret.second;
}

bool ReferralsView::ReferralCodeExists(const uint256& code) const {
    return !(Fetch(code) == m_referral_cache.end());
}

void ReferralsView::Flush() {
    for (auto pr : m_referral_cache) {
        m_db.InsertReferral(pr.second);
    }
    m_referral_cache.clear();
}

