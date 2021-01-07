// Copyright (c) 2017-2021 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_SELECT_H
#define MERIT_POG_SELECT_H

#include "hash.h"
#include "amount.h"
#include "pog/anv.h"
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <map>

namespace referral
{
    class ReferralsViewCache;
}

namespace pog
{
    using StackedAmount = boost::multiprecision::int128_t;

    struct StackedAddressANV
    {
        char address_type;
        referral::Address address;
        StackedAmount anv;
    };

    using InvertedAnvs = std::vector<StackedAddressANV>;
    using WalletToAnv = std::map<referral::Address, referral::AddressANV>;

    class AnvDistribution
    {
        public:
            AnvDistribution(int height, referral::AddressANVs anvs);
            const referral::AddressANV& Sample(const uint256& hash) const;
            size_t Size() const;
            StackedAmount MaxANV() const;

        private:
            InvertedAnvs m_inverted;
            WalletToAnv m_anvs;
            StackedAmount m_max_anv = 0;
    };

    class WalletSelector
    {
        public:
            WalletSelector(int height, const referral::AddressANVs& anvs);

            referral::AddressANVs Select(
                    bool check_confirmations,
                    const referral::ReferralsViewCache& referrals,
                    uint256 hash,
                    size_t n) const;

            size_t Size() const;
            StackedAmount MaxANV() const;
        private:
            AnvDistribution m_distribution;
    };

    referral::ConfirmedAddresses SelectConfirmedAddresses(
            const referral::ReferralsViewDB& db,
            uint256 hash,
            const uint160& genesis_address,
            size_t n,
            std::set<referral::Address> &unconfirmed_invites,
            int max_outstanding_invites);

    /**
     * Returns true if the address type is valid for ambassador lottery.
     */
    bool IsValidAmbassadorDestination(char type);

    StackedAmount GetCachedTotalANV();
} // namespace pog

#endif //MERIT_POG_SELECT_H
