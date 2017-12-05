// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REFERRALS_H
#define REFERRALS_H

#include "primitives/referral.h"
#include "refdb.h"
#include "sync.h"
#include "uint256.h"

#include <unordered_map>
#include <boost/optional.hpp>

namespace referral
{
using ReferralMap = std::unordered_map<Address, Referral, std::hash<uint160>>;
using WalletToReferrer = std::unordered_map<Address, Address, std::hash<uint160>>;

class ReferralsViewCache
{
private:
    mutable CCriticalSection m_cs_cache;
    ReferralsViewDB *m_db;
    mutable ReferralMap m_referral_cache;
    mutable WalletToReferrer m_wallet_to_referrer;

    ReferralMap::iterator Fetch(const Address& address) const;
    void InsertReferralIntoCache(const Referral&) const;
    void InsertWalletRelationshipIntoCache(const Address& child, const Address& parent) const;

public:
    ReferralsViewCache(ReferralsViewDB*);

    MaybeReferral GetReferral(const Address&) const;
    MaybeAddress GetReferrer(const Address& address) const;

    bool ReferralAddressExists(const Address&) const;
    bool WalletIdExists(const Address&) const;

    void RemoveReferral(const Referral&) const;

    void Flush();
};

} // namespace referral

#endif
