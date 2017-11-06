// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

#include "base58.h"

namespace referral
{
namespace
{
const char DB_CHILDREN = 'c';
const char DB_REFERRALS = 'r';
const char DB_REFERRALS_BY_KEY_ID = 'k';
const char DB_PARENT_KEY = 'p';
const char DB_ANV = 'a';
const size_t MAX_LEVELS = 10000000000;
}


ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe, const std::string& db_name) :
    m_db(GetDataDir() / db_name, nCacheSize, fMemory, fWipe, true) {}

MaybeReferral ReferralsViewDB::GetReferral(const uint256& code_hash) const {
     MutableReferral referral;
     return m_db.Read(std::make_pair(DB_REFERRALS,code_hash), referral) ?
         MaybeReferral{referral} : MaybeReferral{};
}

MaybeAddress ReferralsViewDB::GetReferrer(const Address& address) const
{
    Address parent;
    return m_db.Read(std::make_pair(DB_PARENT_KEY, address), parent) ?
        MaybeAddress{parent} : MaybeAddress{};
}

ChildAddresses ReferralsViewDB::GetChildren(const Address& address) const
{
    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, address), children);
    return children;
}

bool ReferralsViewDB::InsertReferral(const Referral& referral) {
    debug("\tInsertReferral: %s -> %s address: %s", referral.m_previousReferral.GetHex(), referral.m_codeHash.GetHex(), EncodeDestination(referral.m_pubKeyId));
    //write referral by code hash
    if(!m_db.Write(std::make_pair(DB_REFERRALS, referral.m_codeHash), referral))
        return false;

    // Typically because the referral should be written in order we should
    // be able to find the parent referral. We can then write the child->parent
    // mapping of public addresses
    Address parent_address;
    if(auto parent_referral = GetReferral(referral.m_previousReferral))
        parent_address = parent_referral->m_pubKeyId;

    if(!m_db.Write(std::make_pair(DB_PARENT_KEY, referral.m_pubKeyId), parent_address))
        return false;

    // Now we update the children of the parent address by inserting into the
    // child address array for the parent.
    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, parent_address), children);

    children.push_back(referral.m_pubKeyId);
    if(!m_db.Write(std::make_pair(DB_CHILDREN, parent_address), children))
        return false;

    return true;
}

bool ReferralsViewDB::RemoveReferral(const Referral& referral) {
    if(!m_db.Erase(std::make_pair(DB_REFERRALS, referral.m_codeHash)))
        return false;

    Address parent_address;
    if(auto parent_referral = GetReferral(referral.m_previousReferral))
        parent_address = parent_referral->m_pubKeyId;

    if(!m_db.Erase(std::make_pair(DB_PARENT_KEY, referral.m_pubKeyId)))
        return false;

    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, parent_address), children);

    children.erase(std::remove(std::begin(children), std::end(children), referral.m_pubKeyId), std::end(children));
    if(!m_db.Write(std::make_pair(DB_CHILDREN, parent_address), children))
        return false;

    return true;
}

bool ReferralsViewDB::ReferralCodeExists(const uint256& code_hash) const {
    return m_db.Exists(std::make_pair(DB_REFERRALS, code_hash));
}

bool ReferralsViewDB::WalletIdExists(const Address& address) const
{
    return m_db.Exists(std::make_pair(DB_PARENT_KEY, address));
}

/**
 * Updates ANV for the address and all parents. Note change can be negative if
 * there was a debit.
 */
bool ReferralsViewDB::UpdateANV(const Address& start_address, CAmount change)
{
    debug("\tUpdateANV: %s + %d", EncodeDestination(start_address), change);
    MaybeAddress address = start_address;
    size_t levels = 0;

    //MAX_LEVELS gaurds against cycles in DB
    while(address && levels < MAX_LEVELS)
    {
        //it's possible address didn't exist yet so an ANV of 0 is assumed.
        CAmount anv = 0;
        m_db.Read(std::make_pair(DB_ANV, *address), anv);

        debug("\t\t %d %s %d + %d", levels, EncodeDestination(*address), anv, change);

        anv += change;
        if(!m_db.Write(std::make_pair(DB_ANV, *address), anv)) {
            //TODO: Do we rollback anv computation for already processed addresss?
            // likely if we can't write then rollback will fail too.
            // figure out how to mark database as corrupt.
            return false;
        }

        address = GetReferrer(*address);
        levels++;
    }

    // We should never have cycles in the DB.
    // Hacked? Bug?
    assert(levels < MAX_LEVELS && "reached max levels. Referral DB cycle detected");
    return true;
}

MaybeANV ReferralsViewDB::GetANV(const Address& address) const
{
    CAmount anv;
    return m_db.Read(std::make_pair(DB_ANV, address), anv) ? MaybeANV{anv} : MaybeANV{};
}

AddressANVs ReferralsViewDB::GetAllANVs() const
{
    std::unique_ptr<CDBIterator> iter{m_db.NewIterator()};
    iter->SeekToFirst();

    AddressANVs anvs;
    auto address = std::make_pair(DB_ANV, Address{});
    while(iter->Valid())
    {
        //filter non ANV addresss
        if(!iter->GetKey(address)) {
            iter->Next();
            continue;
        }

        if(address.first != DB_ANV) {
            iter->Next();
            continue;
        }

        CAmount anv = 0;
        if(!iter->GetValue(anv)) {
            iter->Next();
            continue;
        }

        anvs.push_back({address.second, anv});

        iter->Next();
    }
    return anvs;
}
}
