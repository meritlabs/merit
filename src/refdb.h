// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_REFDB_H
#define MERIT_REFDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "primitives/referral.h"

#include <boost/optional.hpp>
#include <vector>

namespace referral
{
using Address = uint160;
using MaybeReferral = boost::optional<Referral>;
using MaybeAddress = boost::optional<Address>;
using ChildAddresses = std::vector<Address>;
using Addresses = std::vector<Address>;

struct AddressANV
{
    char addressType;
    Address address;
    CAmount anv;
};

using AddressANVs = std::vector<AddressANV>;
using MaybeAddressANV = boost::optional<AddressANV>;

class ReferralsViewDB
{
protected:
    mutable CDBWrapper m_db;
public:
    explicit ReferralsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false, const std::string& name = "referrals");

    MaybeReferral GetReferral(const Address&) const;
    MaybeAddress GetReferrer(const Address&) const;
    ChildAddresses GetChildren(const Address&) const;

    bool UpdateANV(char addressType, const Address&, CAmount);
    MaybeAddressANV GetANV(const Address&) const;
    AddressANVs GetAllANVs() const;
    AddressANVs GetAllRewardableANVs() const;
    bool OrderReferrals(referral::ReferralRefs& refs);

    bool InsertReferral(const Referral&, bool allow_no_parent = false);
    bool RemoveReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;
    bool ReferralAddressExists(const Address&) const;
    bool WalletIdExists(const Address&) const;
};

} // namespace referral

#endif
