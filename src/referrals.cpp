// Copyright (c) 2017-2021 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "referrals.h"

#include <utility>

namespace referral
{
    ReferralsViewCache::ReferralsViewCache(ReferralsViewDB* db) : m_db{db}
    {
        assert(db);
    };

    namespace {
        class ReferralIdVisitor : public boost::static_visitor<MaybeReferral>
        {
            private:
                const ReferralsViewCache *db;
                const bool normalize_alias;

            public:
                ReferralIdVisitor(
                        const ReferralsViewCache *db_in,
                        bool normalize_alias_in) :
                    db{db_in},
                    normalize_alias{normalize_alias_in} {}

                MaybeReferral operator()(const std::string &id) const {
                    return db->GetReferral(id, normalize_alias);
                }

                template <typename T>
                    MaybeReferral operator()(const T &id) const {
                        return db->GetReferral(id);
                    }
        };
    }

    MaybeReferral ReferralsViewCache::GetReferral(const Address& address) const
    {
        {
            LOCK(m_cs_cache);
            auto it = referrals_index.find(address);
            if (it != referrals_index.end()) {
                return *it;
            }
        }

        if (auto ref = m_db->GetReferral(address)) {
            InsertReferralIntoCache(*ref);
            return ref;
        }

        return {};
    }

    MaybeReferral ReferralsViewCache::GetReferral(const uint256& hash) const
    {
        {
            LOCK(m_cs_cache);
            auto it = referrals_index.get<by_hash>().find(hash);
            if (it != referrals_index.get<by_hash>().end()) {
                return *it;
            }
        }

        if (auto ref = m_db->GetReferral(hash)) {
            InsertReferralIntoCache(*ref);
            return ref;
        }

        return {};
    }

    MaybeReferral ReferralsViewCache::GetReferral(const std::string& alias, bool normalize_alias) const
    {
        auto maybe_normalized = alias;

        if (normalize_alias) {
            NormalizeAlias(maybe_normalized);
        }

        if (maybe_normalized.size() == 0) {
            return {};
        }

        {
            LOCK(m_cs_cache);
            auto it = alias_index.find(maybe_normalized);

            if (it != alias_index.end()) {
                return GetReferral(it->second);
            }
        }

        if (auto ref = m_db->GetReferral(maybe_normalized, false)) {
            LOCK(m_cs_cache);
            alias_index[maybe_normalized] = ref->GetAddress();
            InsertReferralIntoCache(*ref);
            return ref;
        }

        return {};
    }

    MaybeReferral ReferralsViewCache::GetReferral(const ReferralId& id, bool normalize_alias) const
    {
        return boost::apply_visitor(ReferralIdVisitor{this, normalize_alias}, id);
    }

    int ReferralsViewCache::GetReferralHeight(const Address& address) const {
        const auto height_p = height_index.find(address);
        if(height_p != height_index.end()) {
            return height_p->second;
        }

        const auto height = m_db->GetReferralHeight(address);
        if(height > 0) {
            height_index[address] = height;
        }
        return height;
    }

    bool ReferralsViewCache::SetReferralHeight(int height, const Address& address) {
        height_index[address] = height;
        return m_db->SetReferralHeight(height, address);
    }

    bool ReferralsViewCache::Exists(const uint256& hash) const
    {
        {
            LOCK(m_cs_cache);
            if (referrals_index.get<by_hash>().count(hash) > 0) {
                return true;
            }
        }

        if (auto ref = m_db->GetReferral(hash)) {
            InsertReferralIntoCache(*ref);
            return true;
        }

        return false;
    }

    bool ReferralsViewCache::Exists(const Address& address) const
    {
        {
            LOCK(m_cs_cache);
            if (referrals_index.count(address) > 0) {
                return true;
            }
        }
        if (auto ref = m_db->GetReferral(address)) {
            InsertReferralIntoCache(*ref);
            return true;
        }
        return false;
    }

    bool ReferralsViewCache::Exists(const std::string& alias, bool normalize_alias) const
    {
        auto maybe_normalized = alias;

        if (normalize_alias) {
            NormalizeAlias(maybe_normalized);
        }

        if (maybe_normalized.size() == 0) {
            return false;
        }

        {
            LOCK(m_cs_cache);
            if (alias_index.count(maybe_normalized) > 0) {
                return true;
            }
        }

        if (auto ref = m_db->GetReferral(maybe_normalized, false)) {
            LOCK(m_cs_cache);
            alias_index[maybe_normalized] = ref->GetAddress();
            InsertReferralIntoCache(*ref);

            return true;
        }

        return false;
    }

    void ReferralsViewCache::InsertReferralIntoCache(const Referral& ref) const
    {
        LOCK(m_cs_cache);
        const auto height = m_db->GetReferralHeight(ref.GetAddress());
        referrals_index.insert(ref);
        if(height > 0) {
            height_index[ref.GetAddress()] = height;
        }
    }

    void ReferralsViewCache::RemoveAliasFromCache(const Referral& ref) const {
        auto normalized_alias = ref.alias;
        NormalizeAlias(normalized_alias);

        if (alias_index.erase(normalized_alias) == 0) {
            alias_index.erase(ref.alias);
        }
    }

    bool ReferralsViewCache::RemoveReferral(const Referral& ref) const
    {
        referrals_index.erase(ref.GetAddress());
        height_index.erase(ref.GetAddress());
        RemoveAliasFromCache(ref);

        return m_db->RemoveReferral(ref);
    }

    bool ReferralsViewCache::UpdateConfirmation(char address_type, const Address& address, CAmount amount)
    {
        assert(m_db);
        CAmount updated_amount;

        if (!m_db->UpdateConfirmation(address_type, address, amount, updated_amount)) {
            return false;
        }

        confirmations_index[address] = updated_amount;

        auto ref = GetReferral(address);

        // if referral was unconfirmed, remove it from the cache
        if (updated_amount == 0) {
            if (!ref) {
                return false;
            }

            RemoveAliasFromCache(*ref);
        }

        return true;
    }

    bool ReferralsViewCache::IsConfirmed(const Address& address) const
    {
        assert(m_db);

        auto it = confirmations_index.find(address);

        if (it != confirmations_index.end()) {
            return it->second > 0;
        }

        return m_db->IsConfirmed(address);
    }

    bool ReferralsViewCache::IsConfirmed(const std::string& alias, bool normalize_alias) const
    {
        assert(m_db);

        auto normalized_alias = alias;

        if (normalize_alias) {
            NormalizeAlias(normalized_alias);
        }

        auto it = alias_index.find(normalized_alias);

        if (it != alias_index.end()) {
            return IsConfirmed(it->second);
        }

        return m_db->IsConfirmed(normalized_alias, false);
    }

    MaybeConfirmedAddress ReferralsViewCache::GetConfirmation(const Address& address) const
    {
        assert(m_db);

        const auto ref = GetReferral(address);

        if (!ref) {
            return MaybeConfirmedAddress{};
        }

        auto it = confirmations_index.find(address);

        if (it != confirmations_index.end()) {
            return MaybeConfirmedAddress{{ref->addressType, address, it->second}};
        }

        return m_db->GetConfirmation(ref->addressType, address);
    }

    MaybeConfirmedAddress ReferralsViewCache::GetConfirmation(uint64_t idx) const
    {
        assert(m_db);
        return m_db->GetConfirmation(idx);
    }

    MaybeConfirmedAddress ReferralsViewCache::GetConfirmation(char address_type, const Address& address) const
    {
        assert(m_db);
        return m_db->GetConfirmation(address_type, address);
    }

    ChildAddresses ReferralsViewCache::GetChildren(const Address& address) const
    {
        assert(m_db);
        return m_db->GetChildren(address);
    }

    uint64_t ReferralsViewCache::GetTotalConfirmations() const
    {
        assert(m_db);
        return m_db->GetTotalConfirmations();
    }

    void ReferralsViewCache::GetAllRewardableANVs(
            const Consensus::Params& params,
            int height,
            AddressANVs& anvs,
            bool cached) const
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if(cached && !all_rewardable_anvs.empty()) {
            anvs = all_rewardable_anvs; 
            return;
        } 

        assert(m_db);
        m_db->GetAllRewardableANVs(params, height, anvs);
        all_rewardable_anvs = anvs;
    }

    bool ReferralsViewCache::SetNewInviteRewardedHeight(const Address& a, int height)
    {
        assert(m_db);
        return m_db->SetNewInviteRewardedHeight(a, height);
    }

    int ReferralsViewCache::GetNewInviteRewardedHeight(const Address& a) const
    {
        assert(m_db);
        return m_db->GetNewInviteRewardedHeight(a);
    }
}
