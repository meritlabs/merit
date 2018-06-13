// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG2_SELECT_H
#define MERIT_POG2_SELECT_H

#include "hash.h"
#include "amount.h"
#include "pog2/anv.h"
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <map>

namespace referral
{
    class ReferralsViewCache;
}

namespace pog2
{
    using InvertedAnvs = referral::AddressANVs;
    using AddressToAnv = std::map<referral::Address, referral::AddressANV>;
    using SampledAddresses = std::set<referral::Address>;

    class AnvDistribution
    {
        public:
            AnvDistribution(int height, referral::AddressANVs anvs);
            const referral::AddressANV& Sample(const uint256& hash) const;
            size_t Size() const;

        private:
            InvertedAnvs m_inverted;
            AddressToAnv m_anvs;
            SampledAddresses m_sampled;
            CAmount m_max_anv = 0;
    };

    class AddressSelector
    {
        public:
            AddressSelector(int height, const referral::AddressANVs& anvs);

            referral::AddressANVs Select(
                    bool check_confirmations,
                    const referral::ReferralsViewCache& referrals,
                    uint256 hash,
                    size_t n) const;

            size_t Size() const;
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
} // namespace pog2

#endif //MERIT_POG2_SELECT_H
