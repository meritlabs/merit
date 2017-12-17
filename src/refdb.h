// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_REFDB_H
#define MERIT_REFDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "primitives/referral.h"

#include <boost/optional.hpp>
#include <boost/multiprecision/float128.hpp> 
#include <vector>

namespace referral
{
using Address = uint160;
using MaybeReferral = boost::optional<Referral>;
using MaybeAddress = boost::optional<Address>;
using ChildAddresses = std::vector<Address>;
using Addresses = std::vector<Address>;
using WeightedKey = boost::multiprecision::float128;
using MaybeWeightedKey = boost::optional<WeightedKey>;

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

    MaybeReferral GetReferral(const uint256&) const;
    MaybeAddress GetReferrer(const Address&) const;
    ChildAddresses GetChildren(const Address&) const;

    bool UpdateANV(char addressType, const Address&, CAmount);
    MaybeAddressANV GetANV(const Address&) const;
    AddressANVs GetAllANVs() const;

    bool InsertReferral(const Referral&);
    bool RemoveReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;
    bool WalletIdExists(const Address&) const;

    AddressANVs GetAllRewardableANVs() const;
    bool AddAddressToLottery(const uint256&, const Address&);

private:
    std::size_t GetLotteryHeapSize() const;
    MaybeWeightedKey GetLotteryMinKey() const;
    bool InsertLotteryAddress(const WeightedKey& key, const Address& address);

};

} // namespace referral

#endif
