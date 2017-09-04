// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(GetDataDir() / "referrals", nCacheSize, fMemory, fWipe, true) {}

bool ReferralsViewDB::GetReferral(const uint256& code_hash, MutableReferral& referral) const {
    return m_db.Read(code_hash, referral);
}

bool ReferralsViewDB::InsertReferral(const Referral& referral) {
    return m_db.Write(referral.m_codeHash, referral);
}

bool ReferralsViewDB::ReferralCodeExists(const uint256& code_hash) const {
    return m_db.Exists(code_hash);
}
