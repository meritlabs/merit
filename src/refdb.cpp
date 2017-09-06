// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

namespace 
{
const char DB_CHILDREN = 'c';
const char DB_REFERRALS = 'r';
const char DB_PARENT_KEY = 'p';
}

ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(GetDataDir() / "referrals", nCacheSize, fMemory, fWipe, true) {}

MaybeReferral ReferralsViewDB::GetReferral(const uint256& code_hash) const {
     MutableReferral referral;
     return m_db.Read(std::make_pair(DB_REFERRALS,code_hash), referral) ? 
         MaybeReferral{referral} : MaybeReferral{};
}

MaybeKeyID ReferralsViewDB::GetReferrer(const CKeyID& key) const
{
    CKeyID parent;
    return m_db.Read(std::make_pair(DB_PARENT_KEY, key), parent) ? 
        MaybeKeyID{parent} : MaybeKeyID{};
}

ChildKeys ReferralsViewDB::GetChildren(const CKeyID& key) const
{
    ChildKeys children;
    m_db.Read(std::make_pair(DB_CHILDREN, key), children);
    return children;
}

bool ReferralsViewDB::InsertReferral(const Referral& referral) {
    //write referral
    if(!m_db.Write(std::make_pair(DB_REFERRALS, referral.m_codeHash), referral))
        return false;

    // Typically because the referral should be written in order we should
    // be able to find the parent referral. We can then write the child->parent
    // mapping of public keys
    CKeyID parent_key;
    if(auto parent_referral = GetReferral(referral.m_previousReferral))
        parent_key = parent_referral->m_pubKeyId;

    if(!m_db.Write(std::make_pair(DB_PARENT_KEY, referral.m_pubKeyId), parent_key))
        return false;

    // Now we update the children of the parent key by inserting into the 
    // child key array for the parent.
    ChildKeys children;
    m_db.Read(std::make_pair(DB_CHILDREN, parent_key), children);

    children.push_back(referral.m_pubKeyId);
    if(!m_db.Write(std::make_pair(DB_CHILDREN, parent_key), children))
        return false;

    return true;
}

bool ReferralsViewDB::ReferralCodeExists(const uint256& code_hash) const {
    return m_db.Exists(std::make_pair(DB_REFERRALS, code_hash));
}

bool ReferralsViewDB::WalletIdExists(const CKeyID& key) const
{
    return m_db.Exists(std::make_pair(DB_PARENT_KEY, key));
}
