// Copyright (c) 2017-2018 The Merit Foundation developers
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
        // stored by beaconed address
        hashed_unique<tag<by_address>, const_mem_fun<Referral, const Address&, &Referral::GetAddress>, SaltedHasher<160>>,
        // stored by hash
        // use non-unique here to support empty tags.
        // otherwise it won't add such referrals to index
        // uniqueness is provided by validation
        hashed_non_unique<tag<by_hash>, const_mem_fun<Referral, const uint256&, &Referral::GetHash>, SaltedHasher<256>>>>;

using AliasIndex = std::unordered_map<std::string, Address>;
using ConfirmationsIndex = std::unordered_map<Address, int>;
using HeightIndex = std::unordered_map<Address, int>;

class ReferralsViewCache
{
private:
    mutable CCriticalSection m_cs_cache;
    ReferralsViewDB* m_db;
    mutable ReferralIndex referrals_index;
    mutable AliasIndex alias_index;
    mutable ConfirmationsIndex confirmations_index;
    mutable ConfirmationsIndex height_index;

    void InsertReferralIntoCache(const Referral&) const;
    void RemoveAliasFromCache(const Referral&) const;

public:
    ReferralsViewCache(ReferralsViewDB*);

    /** Get referral by address */
    MaybeReferral GetReferral(const Address&) const;

    /** Get referral by hash */
    MaybeReferral GetReferral(const uint256&) const;

    /** Get referral by alias */
    MaybeReferral GetReferral(const std::string& alias, bool normalize_alias) const;

    /** Get referral by address */
    MaybeReferral GetReferral(const ReferralId&, bool normalize_alias) const;

    /** Returns the height of a referral in the blockchain */
    int GetReferralHeight(const Address&) const;

    /** Sets the height of a referral in the blockchain */
    bool SetReferralHeight(int height, const Address& ref);

    /** Check if referral exists by beaconed address */
    bool Exists(const Address&) const;

    /** Check if referral exists by hash */
    bool Exists(const uint256&) const;

    /** Check if referral alias occupied */
    bool Exists( const std::string& alias, bool normalize_alias) const;

    /** Remove referral from cache */
    bool RemoveReferral(const Referral&) const;

    /** Flush referrals to disk and clear cache */
    void Flush();

    /** Update number of confirmations for referral */
    bool UpdateConfirmation(char address_type, const Address& address, CAmount amount);

    /** Check if an address is confirmed */
    bool IsConfirmed(const Address&) const;

    /** Check if an address is confirmed */
    bool IsConfirmed(const std::string& alias, bool normalize_alias) const;

    /** Returns total confirmed beacons*/
    uint64_t GetTotalConfirmations() const;

    // Get address confirmations
    MaybeConfirmedAddress GetConfirmation(const Address& address) const;
    MaybeConfirmedAddress GetConfirmation(uint64_t idx) const;
    MaybeConfirmedAddress GetConfirmation(char address_type, const Address& address) const;

    void GetAllRewardableANVs(
            const Consensus::Params& params,
            int height,
            AddressANVs&) const;

    ChildAddresses GetChildren(const Address&) const;

    /** A novite is the oldest beacon with 1 invite. */
    uint64_t GetOldestNoviteIdx() const;
    bool SetOldestNoviteIdx(uint64_t idx) const;
};

} // namespace referral

#endif
