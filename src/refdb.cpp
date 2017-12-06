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
const char DB_HASH = 'h';
const char DB_PARENT_KEY = 'p';
const char DB_ANV = 'a';
const size_t MAX_LEVELS = std::numeric_limits<size_t>::max();
}

using ANVTuple = std::tuple<char, Address, CAmount>;

ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe, const std::string& db_name) :
    m_db(GetDataDir() / db_name, nCacheSize, fMemory, fWipe, true) {}

MaybeReferral ReferralsViewDB::GetReferral(const Address& address) const {
     MutableReferral referral;
     return m_db.Read(std::make_pair(DB_REFERRALS, address), referral) ?
         MaybeReferral{referral} : MaybeReferral{};
}

MaybeReferral ReferralsViewDB::GetReferral(const uint256& hash) const
{
    Address address;
    if (m_db.Read(std::make_pair(DB_HASH, hash), address)) {
        return GetReferral(address);
    }

    return {};
}

bool ReferralsViewDB::exists(const referral::Address& address) const {
    return m_db.Exists(std::make_pair(DB_REFERRALS, address));
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
    // write referral by address
    if(!m_db.Write(std::make_pair(DB_REFERRALS, referral.address), referral))
        return false;

    // write referral address by hash
    if(!m_db.Write(std::make_pair(DB_HASH, referral.GetHash()), referral.address))
        return false;

    // Typically because the referral should be written in order we should
    // be able to find the parent referral. We can then write the child->parent
    // mapping of public addresses
    if(!m_db.Write(std::make_pair(DB_PARENT_KEY, referral.address), referral.parentAddress))
        return false;

    // Now we update the children of the parent address by inserting into the
    // child address array for the parent.
    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, referral.parentAddress), children);

    children.push_back(referral.address);
    if(!m_db.Write(std::make_pair(DB_CHILDREN, referral.parentAddress), children))
        return false;

    return true;
}

bool ReferralsViewDB::RemoveReferral(const Referral& referral) {
    if(!m_db.Erase(std::make_pair(DB_REFERRALS, referral.address)))
        return false;

    if(!m_db.Erase(std::make_pair(DB_HASH, referral.GetHash())))
        return false;

    if(!m_db.Erase(std::make_pair(DB_PARENT_KEY, referral.address)))
        return false;

    ChildAddresses children;
    m_db.Read(std::make_pair(DB_CHILDREN, referral.parentAddress), children);

    children.erase(std::remove(std::begin(children), std::end(children), referral.address), std::end(children));
    if(!m_db.Write(std::make_pair(DB_CHILDREN, referral.parentAddress), children))
        return false;

    return true;
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
    while(address && levels < MAX_LEVELS)
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
