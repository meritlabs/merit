// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REFERRALS_H
#define REFERRALS_H

#include "hash.h"
#include "primitives/referral.h"
#include "random.h"
#include "refdb.h"
#include "sync.h"
#include "uint256.h"
#include "utilstrencodings.h"

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <unordered_map>

using namespace boost::multi_index;

namespace referral
{
template <unsigned int BITS>
class SaltedHasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

    size_t operator()(const base_blob<BITS>& data) const
    {
        return CSipHasher(k0, k1).Write(data.begin(), data.size()).Finalize();
    }
};

// multi_index tags
struct by_address {
};
struct by_hash {
};


using ReferralIndex = multi_index_container<
    Referral,
    indexed_by<
        // sorted by beaconed address
        hashed_unique<tag<by_address>, const_mem_fun<Referral, const Address&, &Referral::GetAddress>, SaltedHasher<160>>,
        // sorted by hash
        hashed_unique<tag<by_hash>, const_mem_fun<Referral, const uint256&, &Referral::GetHash>, SaltedHasher<256>>>>;

class ReferralsViewCache
{
private:
    mutable CCriticalSection m_cs_cache;
    ReferralsViewDB* m_db;
    mutable ReferralIndex referrals_index;

    void InsertReferralIntoCache(const Referral&) const;

public:
    ReferralsViewCache(ReferralsViewDB*);

    /** Get referral by address */
    MaybeReferral GetReferral(const Address&) const;

    /** Check if referral exists by beaconed address */
    bool Exists(const Address&) const;

    /** Check if referral exists by hash */
    bool Exists(const uint256&) const;

    /** Remove referral from cache */
    void RemoveReferral(const Referral&) const;

    /** Flush referrals to disk and clear cache */
    void Flush();

    /** Check if an address is confirmed */
    bool IsConfirmed(const Address&) const;
};

} // namespace referral

#endif
