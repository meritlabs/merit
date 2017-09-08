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
     * CDF is trivial for for the ANV discrete distribution is easy 
     * by simply sorting and adding up all the ANVs of the keys provided.
     *
     * Scaling to probabilities is unnecessary because we will use a hash function
     * to sample into the range between 0-MaxAnv. Since the hash is already
     * a uniform distribution then it provides a good way to sample into
     * the distribution of ANVs where those with a bigger ANV are sampled more often. 
     *
     * The most expensive part of creating the distribution is sorting the ANVs
     */
    AnvDistribution::AnvDistribution(KeyANVs anvs) : m_inverted(anvs.size())
    {
        //It doesn't make sense to sample from an empty distribution.
        assert(anvs.empty() == false);

        //index anvs by key id for convenience. 
        std::transform(std::begin(anvs), std::end(anvs), std::inserter(m_anvs, std::begin(m_anvs)),
                [](const KeyANV& anv) {
                    return std::make_pair(anv.key, anv);
                });

        assert(m_anvs.size() == anvs.size());
        
        //sort from lowest to highest
        std::sort(std::begin(anvs), std::end(anvs), 
                [](const KeyANV& a, const KeyANV& b) { 
                    return a.anv < b.anv;
                });

        assert(m_inverted.size() == anvs.size());

        //compute CDF by adding up all the ANVs 
        CAmount previous_anv = 0;
        std::transform(std::begin(anvs), std::end(anvs), std::begin(m_inverted),
                [&previous_anv](KeyANV w) {
                    w.anv += previous_anv;
                    previous_anv = w.anv;
                    return w;
                });

        //back will always return because we assume m_anvs is non-empty
        m_max_anv = m_inverted.back().anv;

        assert(m_max_anv >= 0);
    }

    const KeyANV& AnvDistribution::Sample(const uint256& hash) const
    {
        assert(m_inverted.empty() == false);

        //TODO: Should we loop over whole hash?
        CAmount selected_anv = hash.GetUint64(0) % m_max_anv;

        //find first inverted Wallet Anv that is greater or equal to the selected value.
        auto pos = std::lower_bound(std::begin(m_inverted), std::end(m_inverted), 
                selected_anv,
                [](const KeyANV& a, CAmount selected) {
                    return a.anv < selected;
                });

        assert(selected_anv < m_max_anv);
        assert(pos != std::end(m_inverted)); //it should be impossible to not find an anv
                                             //because selected_anv must be less than max
        auto selected_key = m_anvs.find(pos->key);

        assert(selected_key != std::end(m_anvs)); //all anvs in m_inverted must be in 
                                                    //our index

        return selected_key->second;
    }

    size_t AnvDistribution::Size() const { 
        return m_inverted.size();
    }

    WalletSelector::WalletSelector(const KeyANVs& anvs) : m_distribution{anvs} {}

    /**
     * Selecting winners from the distribution is deterministic and will return the same
     * N samples given the same input hash.
     */
    KeyANVs WalletSelector::Select(uint256 hash, size_t n) const
    {
        assert(n <= m_distribution.Size());
        KeyANVs samples;

        while(n--) {
            const auto& sampled = m_distribution.Sample(hash);

            //combine hashes and hash to get next sampling value
            CHashWriter hasher{SER_DISK, CLIENT_VERSION};
            hasher << hash << sampled.key;
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
