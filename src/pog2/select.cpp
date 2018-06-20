// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog2/select.h"
#include "base58.h"
#include "clientversion.h"
#include <algorithm>
#include <iterator>
#include "referrals.h"

namespace pog2
{
    bool IsValidAmbassadorDestination(char type)
    {
        //KeyID or ScriptID
        return type == 1 || type == 2;
    }

    /**
     * CgsDistribution uses Inverse Transform Sampling. Computing the
     * CDF is trivial for the CGS discrete distribution by simply sorting and
     * adding up all the CGSs of the addresss provided.
     *
     * Scaling to probabilities is unnecessary because we will use a hash function
     * to sample into the range between 0-MaxCgs. Since the hash is already
     * a uniform distribution then it provides a good way to sample into
     * the distribution of CGSs where those with a bigger CGS are sampled more often.
     *
     * The most expensive part of creating the distribution is sorting the CGSs.
     * However, since the number of CGSs is fixed no matter how large the
     * blockchain gets, then there should be no issue handling growth.
     */
    CgsDistribution::CgsDistribution(pog2::Entrants cgses) :
        m_inverted(cgses.size())
    {
        //index cgses by address id for convenience.
        std::transform(
                std::begin(cgses),
                std::end(cgses),
                std::inserter(m_cgses, std::begin(m_cgses)),

                [](const pog2::Entrant& v) {
                    assert(v.cgs >= 0);
                    return std::make_pair(v.address, v);
                });

        assert(m_cgses.size() == cgses.size());

        std::sort(std::begin(cgses), std::end(cgses),
                [](const pog2::Entrant& a, const pog2::Entrant& b) {
                    return a.cgs == b.cgs ? a.address < b.address : a.cgs < b.cgs;
                });

        assert(m_inverted.size() == cgses.size());

        //compute CDF by adding up all the CGSs
        CAmount previous_cgs = 0;
        std::transform(std::begin(cgses), std::end(cgses), std::begin(m_inverted),
                [&previous_cgs](pog2::Entrant w) {
                    w.cgs += previous_cgs;
                    previous_cgs = w.cgs;
                    return w;
                });

        //back will always return because we assume m_cgses is non-empty
        if(!m_inverted.empty()) m_max_cgs = m_inverted.back().cgs;

        assert(m_max_cgs >= 0);
    }

    const pog2::Entrant& CgsDistribution::Sample(const uint256& hash) const
    {
        //It doesn't make sense to sample from an empty distribution.
        assert(m_inverted.empty() == false);

        const auto selected_cgs = SipHashUint256(0, 0, hash) % m_max_cgs;

        auto pos = std::lower_bound(std::begin(m_inverted), std::end(m_inverted),
                selected_cgs,
                [](const pog2::Entrant& a, CAmount selected) {
                    return a.cgs < selected;
                });

        assert(m_max_cgs >= 0);
        assert(selected_cgs < static_cast<uint64_t>(m_max_cgs));
        assert(pos != std::end(m_inverted)); //it should be impossible to not find an cgs
                                             //because selected_cgs must be less than max
        auto selected_address = m_cgses.find(pos->address);

        assert(selected_address != std::end(m_cgses)); //all cgses in m_inverted must be in
                                                    //our index
        return selected_address->second;
    }

    size_t CgsDistribution::Size() const {
        return m_inverted.size();
    }

    AddressSelector::AddressSelector(
            int height,
            const pog2::Entrants& entrants,
            const Consensus::Params& params)
    {
        m_old_distribution.reset(new CgsDistribution{entrants});

        pog2::Entrants new_entrants;
        std::copy_if(entrants.begin(), entrants.end(), std::back_inserter(new_entrants),
                [height,&params](const Entrant& e) {
                    assert(height >= e.beacon_height);
                    const double age = height - e.beacon_height;
                    return age < params.pog2_new_distribution_age;

                });
        m_new_distribution.reset(new CgsDistribution{new_entrants});

        assert(m_old_distribution);
        assert(m_new_distribution);
    }

    /**
     * Selecting winners from the distribution is deterministic and will return the same
     * N samples given the same input hash.
     */
    pog2::Entrants AddressSelector::Select(
            const referral::ReferralsViewCache& referrals,
            uint256 hash,
            size_t n,
            const CgsDistribution& distribution) const
    {
        assert(n <= Size());
        pog2::Entrants samples;

        auto max_tries = std::min(std::max(n, distribution.Size() / 2), distribution.Size());
        while(n-- && max_tries--) {
            const auto& sampled = distribution.Sample(hash);

            //combine hashes and hash to get next sampling value
            CHashWriter hasher{SER_DISK, CLIENT_VERSION};
            hasher << hash << sampled.address;
            hash = hasher.GetHash();

            if(referrals.IsConfirmed(sampled.address)) {
                samples.push_back(sampled);
            } else {
                n++;
            }
        }

        return samples;
    }

    pog2::Entrants AddressSelector::SelectOld(
            const referral::ReferralsViewCache& referrals,
            uint256 hash,
            size_t n) const
    {
        assert(m_old_distribution);
        return Select( referrals, hash, n, *m_old_distribution);
    }
        
    pog2::Entrants AddressSelector::SelectNew(
            const referral::ReferralsViewCache& referrals,
            uint256 hash,
            size_t n) const
    {
        assert(m_new_distribution);
        return Select( referrals, hash, n, *m_new_distribution);
    }

    size_t AddressSelector::Size() const
    {
        assert(m_old_distribution);
        assert(m_new_distribution);
        assert(m_old_distribution->Size() == m_new_distribution->Size());

        return m_old_distribution->Size();
    }

    referral::ConfirmedAddresses SelectConfirmedAddresses(
            const referral::ReferralsViewDB& db,
            uint256 hash,
            const uint160& genesis_address,
            size_t n,
            std::set<referral::Address> &unconfirmed_invites,
            int max_outstanding_invites)
    {
        assert(n > 0);
        assert(max_outstanding_invites > 0);

        auto requested = n;

        const auto total = db.GetTotalConfirmations();
        auto max_tries = std::min(std::max(static_cast<uint64_t>(n), total / 10), total);
        assert(total > 0);

        referral::ConfirmedAddresses addresses;

        while(n-- && max_tries--) {
            const auto selected_idx = SipHashUint256(0, 0, hash) % total;
            const auto sampled = db.GetConfirmation(selected_idx);

            if(!sampled) {
                return {};
            }

            if (!IsValidAmbassadorDestination(sampled->address_type)) {
                n++;
            } else if (sampled->invites == 0) {
                n++;
            } else if (sampled->invites > max_outstanding_invites) {
                n++;
            } else if (sampled->address == genesis_address) {
                n++;
            } else if (unconfirmed_invites.count(sampled->address)) {
                n++;
            } else {
                addresses.push_back(*sampled);
            }

            CHashWriter hasher{SER_DISK, CLIENT_VERSION};
            hasher << hash << sampled->address;
            hash = hasher.GetHash();
        }

        if(requested > 0) {
            LogPrintf("Selected %d addresses (requested %d) for the invite lottery from a pool of %d\n", addresses.size(), requested, total);
        }

        assert(addresses.size() <= requested);
        return addresses;
    }

} // namespace pog2
