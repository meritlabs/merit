// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

namespace 
{
const char DB_CHILDREN = 'c';
const char DB_REFERRALS = 'r';
const char DB_PARENT_KEY = 'p';
const char DB_ANV = 'a';
const size_t MAX_LEVELS = 10000000000;
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

/**
 * Updates ANV for the key and all parents. Note change can be negative if 
 * there was a debit.
 */
bool ReferralsViewDB::UpdateANV(const CKeyID& start_key, CAmount change)
{
    MaybeKeyID key = start_key;
    size_t levels = 0;

    //MAX_LEVELS gaurds against cycles in DB
    while(key && levels < MAX_LEVELS)
    {
        //it's possible key didn't exist yet so an ANV of 0 is assumed.
        CAmount anv = 0;
        m_db.Read(std::make_pair(DB_ANV, *key), anv);

        anv += change;
        if(!m_db.Write(std::make_pair(DB_ANV, *key), anv)) {
            //TODO: Do we rollback anv computation for already processed keys?
            // likely if we can't write then rollback will fail too. 
            // figure out how to mark database as corrupt.
            return false;
        }

        key = GetReferrer(*key);
        levels++;
    }

    // We should never have cycles in the DB. 
    // Hacked? Bug?
    assert(levels < MAX_LEVELS && "reached max levels. Referral DB cycle detected");
    return true;
}

MaybeANV ReferralsViewDB::GetANV(const CKeyID& key) const
{
    CAmount anv;
    return m_db.Read(std::make_pair(DB_ANV, key), anv) ? MaybeANV{anv} : MaybeANV{};
}

KeyANVs ReferralsViewDB::GetAllANVs() const
{
    std::unique_ptr<CDBIterator> iter{m_db.NewIterator()};
    iter->SeekToFirst();

    KeyANVs anvs;
    auto key = std::make_pair(DB_ANV, CKeyID{});
    while(iter->Valid())
    {
        //filter non ANV keys
        if(!iter->GetKey(key))
            continue;
        if(key.first != DB_ANV)
            continue;

        CAmount anv = 0;
        if(!iter->GetValue(anv))
            continue;

        anvs.push_back({key.second, anv});
    }
    return anvs;
}
