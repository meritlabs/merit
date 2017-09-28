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

using ReferralMap = std::unordered_map<uint256, Referral>;
using WalletToReferrer = std::unordered_map<CKeyID, CKeyID, std::hash<uint160>>;

class ReferralsViewCache
{
private:
    mutable CCriticalSection m_cs_cache;
    ReferralsViewDB *m_db;
    mutable ReferralMap m_referral_cache;
    mutable WalletToReferrer m_wallet_to_referrer;

    ReferralMap::iterator Fetch(const uint256& code) const;
    void InsertReferralIntoCache(const Referral&) const;
    void InsertWalletRelationshipIntoCache(const CKeyID& child, const CKeyID& parent) const;

public:
    ReferralsViewCache(ReferralsViewDB*);

    MaybeReferral GetReferral(const uint256&) const;
    MaybeKeyID GetReferrer(const CKeyID& key) const;

    bool ReferralCodeExists(const uint256&) const;
    bool WalletIdExists(const CKeyID&) const;

    void RemoveReferral(const Referral&) const;

    void Flush();
};

#endif
