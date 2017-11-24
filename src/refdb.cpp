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
const size_t MAX_LEVELS = std::numeric_limits<size_t>::max();
}

using ANVTuple = std::tuple<char, Address, CAmount>;

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
    //write referral by code hash
    if(!m_db.Write(std::make_pair(DB_REFERRALS, referral.codeHash), referral))
        return false;

    // Typically because the referral should be written in order we should
    // be able to find the parent referral. We can then write the child->parent
    // mapping of public addresses
    Address parent_address;
    if(auto parent_referral = GetReferral(referral.previousReferral))
        parent_address = parent_referral->pubKeyId;

    if(!m_db.Write(std::make_pair(DB_PARENT_KEY, referral.pubKeyId), parent_address))
        return false;

    // Now we update the children of the parent address by inserting into the
    // child address array for the parent.
    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, parent_address), children);

    children.push_back(referral.pubKeyId);
    if(!m_db.Write(std::make_pair(DB_CHILDREN, parent_address), children))
        return false;

    return true;
}

bool ReferralsViewDB::RemoveReferral(const Referral& referral) {
    if(!m_db.Erase(std::make_pair(DB_REFERRALS, referral.codeHash)))
        return false;

    Address parent_address;
    if(auto parent_referral = GetReferral(referral.previousReferral))
        parent_address = parent_referral->pubKeyId;

    if(!m_db.Erase(std::make_pair(DB_PARENT_KEY, referral.pubKeyId)))
        return false;

    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, parent_address), children);

    children.erase(std::remove(std::begin(children), std::end(children), referral.pubKeyId), std::end(children));
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

bool ReferralsViewDB::UpdateANV(char addressType, const Address& start_address, CAmount change)
{
    debug("\tUpdateANV: %s + %d", CMeritAddress(addressType, start_address).ToString(), change);
    MaybeAddress address = start_address;
    size_t levels = 0;

    //MAX_LEVELS guards against cycles in DB
    while(address && change != 0 && levels < MAX_LEVELS)
    {
        //it's possible address didn't exist yet so an ANV of 0 is assumed.
        ANVTuple anv;
        m_db.Read(std::make_pair(DB_ANV, *address), anv);

        if(levels == 0) {
            std::get<0>(anv) = addressType;
            std::get<1>(anv) = start_address;
        }

        debug(
                "\t\t %d %s %d + %d",
                levels,
                CMeritAddress(std::get<0>(anv),std::get<1>(anv)).ToString(),
                std::get<2>(anv),
                change);

        std::get<2>(anv) += change;
        change /= 2;

        assert(std::get<2>(anv) >= 0);

        if(!m_db.Write(std::make_pair(DB_ANV, *address), anv)) {
            //TODO: Do we rollback anv computation for already processed address?
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

MaybeAddressANV ReferralsViewDB::GetANV(const Address& address) const
{
    ANVTuple anv;
    return m_db.Read(std::make_pair(DB_ANV, address), anv) ? 
        MaybeAddressANV{{ std::get<0>(anv), std::get<1>(anv), std::get<2>(anv) }} : 
        MaybeAddressANV{};
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

        ANVTuple anv;
        if(!iter->GetValue(anv)) {
            iter->Next();
            continue;
        }

        anvs.push_back({
                std::get<0>(anv),
                std::get<1>(anv),
                std::get<2>(anv)
                });

        iter->Next();
    }
    return anvs;
}

AddressANVs ReferralsViewDB::GetAllRewardableANVs() const
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

        ANVTuple anv;
        if(!iter->GetValue(anv)) {
            iter->Next();
            continue;
        }

        const auto addressType = std::get<0>(anv);
        if(addressType != 1 && addressType != 2) {
            iter->Next();
            continue;
        }

        anvs.push_back({
                addressType,
                std::get<1>(anv),
                std::get<2>(anv)
                });

        iter->Next();
    }
    return anvs;
}
}
