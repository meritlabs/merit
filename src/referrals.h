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
// TODO: rewrite to boost::multi_index_container
using ReferralMap = std::unordered_map<Address, Referral, std::hash<uint160>>;
using ReferralHashMap = std::unordered_map<uint256, Referral>;

class ReferralsViewCache
{
private:
    mutable CCriticalSection m_cs_cache;
    ReferralsViewDB *m_db;
    mutable ReferralMap m_referral_cache;
    mutable ReferralHashMap m_referral_hash_cache;

    void InsertReferralIntoCache(const Referral&) const;
    void InsertWalletRelationshipIntoCache(const Address& child, const Address& parent) const;

public:
    ReferralsViewCache(ReferralsViewDB*);

    /** Get referral by address */
    MaybeReferral GetReferral(const Address&) const;

    /** Check if referral exists by beaconed address */
    bool exists(const Address&) const;

    /** Check if referral exists by hash */
    bool exists(const uint256&) const;

    /** Remove referral from cache */
    void RemoveReferral(const Referral&) const;

    /** Flush referrals to disk and clear cache */
    void Flush();
};

} // namespace referral

#endif
