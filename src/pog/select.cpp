// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/select.h"
#include "clientversion.h"
#include <algorithm>
#include <iterator>

namespace pog
{
    /**
     * AnvDistribution uses Inverse Transform Sampling. Computing the
     * CDF is trivial for the ANV discrete distribution by simply sorting and 
     * adding up all the ANVs of the addresss provided.
     *
     * Scaling to probabilities is unnecessary because we will use a hash function
     * to sample into the range between 0-MaxAnv. Since the hash is already
     * a uniform distribution then it provides a good way to sample into
     * the distribution of ANVs where those with a bigger ANV are sampled more often.
     *
     * The most expensive part of creating the distribution is sorting the ANVs.
     * However, since the number of ANVs is fixed no matter how large the 
     * blockchain gets, then there should be no issue handling growth.
     */
    AnvDistribution::AnvDistribution(referral::AddressANVs anvs) : 
        m_inverted(anvs.size())
    {
        //index anvs by address id for convenience. 
        std::transform(
                std::begin(anvs),
                std::end(anvs),
                std::inserter(m_anvs, std::begin(m_anvs)),

                [](const referral::AddressANV& v) {
                    assert(v.anv >= 0);
                    return std::make_pair(v.address, v);
                });

        assert(m_anvs.size() == anvs.size());

        std::sort(std::begin(anvs), std::end(anvs),
                [](const referral::AddressANV& a, const referral::AddressANV& b) {
                    return a.anv < b.anv;
                });

        assert(m_inverted.size() == anvs.size());

        //compute CDF by adding up all the ANVs
        CAmount previous_anv = 0;
        std::transform(std::begin(anvs), std::end(anvs), std::begin(m_inverted),
                [&previous_anv](referral::AddressANV w) {
                    w.anv += previous_anv;
                    previous_anv = w.anv;
                    return w;
                });

        //back will always return because we assume m_anvs is non-empty
        if(!m_inverted.empty()) m_max_anv = m_inverted.back().anv;

        assert(m_max_anv >= 0);
    }

    const referral::AddressANV& AnvDistribution::Sample(const uint256& hash) const
    {
        //It doesn't make sense to sample from an empty distribution.
        assert(m_inverted.empty() == false);

        const auto selected_anv = SipHashUint256(0, 0, hash) % m_max_anv;

        auto pos = std::lower_bound(std::begin(m_inverted), std::end(m_inverted),
                selected_anv,
                [](const referral::AddressANV& a, CAmount selected) {
                    return a.anv < selected;
                });

        assert(m_max_anv >= 0);
        assert(selected_anv < static_cast<uint64_t>(m_max_anv));
        assert(pos != std::end(m_inverted)); //it should be impossible to not find an anv
                                             //because selected_anv must be less than max
        auto selected_address = m_anvs.find(pos->address);

        assert(selected_address != std::end(m_anvs)); //all anvs in m_inverted must be in
                                                    //our index
        return selected_address->second;
    }

    size_t AnvDistribution::Size() const {
        return m_inverted.size();
    }

    WalletSelector::WalletSelector(const referral::AddressANVs& anvs) : m_distribution{anvs} {}

    /**
     * Selecting winners from the distribution is deterministic and will return the same
     * N samples given the same input hash.
     */
    referral::AddressANVs WalletSelector::Select(uint256 hash, size_t n) const
    {
        assert(n <= m_distribution.Size());
        referral::AddressANVs samples;

        while(n--) {
            const auto& sampled = m_distribution.Sample(hash);

            //combine hashes and hash to get next sampling value
            CHashWriter hasher{SER_DISK, CLIENT_VERSION};
            hasher << hash << sampled.address;
            hash = hasher.GetHash();

            samples.push_back(sampled);
        }

        return samples;
    }

    size_t WalletSelector::Size() const
    {
        return m_distribution.Size();
    }

} // namespace pog
