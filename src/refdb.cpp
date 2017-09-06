// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

namespace 
{
const char DB_REF_NUM_CHILDREN_PREFIX = 'n';
const char DB_REFERRALS = 'r';
const char DB_PARENT_KEY = 'p';
}

ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(GetDataDir() / "referrals", nCacheSize, fMemory, fWipe, true) {}

MaybeReferral ReferralsViewDB::GetReferral(const uint256& code_hash) const {
     MutableReferral referral;
     return m_db.Read(std::make_pair(DB_REFERRALS,code_hash), referral) ? referral : {};
}

bool ReferralsViewDB::InsertReferral(const Referral& referral) {
    //write referral
    bool referral_written = m_db.Write(std::make_pair(DB_REFERRALS, referral.m_codeHash), referral);
    if(!referral_written) return false;

    // Typically because the referral should be writen in order we should
    // be able to find the parent referral. We can then write the child->parent
    // mapping of public keys
    if(auto parent_referral = GetReferral(referral.m_previousReferral, previous_referral); parent_referral)
        return m_db.Write(std::make_pair(DB_PARENT_KEY, referral.m_pubKeyId), previous_referral.m_pubKeyId);
    else
        return m_db.Write(std::make_pair(DB_PARENT_KEY, referral.m_pubKeyId), CKeyID{});

    return true;
}

bool ReferralsViewDB::ReferralCodeExists(const uint256& code_hash) const {
    return m_db.Exists(std::make_pair(DB_REFERRALS, code_hash));
}

MaybeKeyID ReferralsViewDB::GetRefferer(const CKeyID& key) const
{
    CKeyID parent;
    return m_db.Read(std::make_pair(DB_PARENT_KEY, key), parent) ? parent : {};
}

bool ReferralsViewDB::WalletIdExists(const CKeyID& key) const
{
    return m_db.Exists(std::make_pair(DB_PARENT_KEY, key));
}
