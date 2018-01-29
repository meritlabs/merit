// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/select.h"
#include "clientversion.h"
#include <algorithm>
#include <iterator>

namespace pog
{
    namespace
    {
        bool LegacyAnvCmp(
                const referral::AddressANV& a,
                const referral::AddressANV& b)
        {
            return a.anv < b.anv;
        };

        void MoveMedian(
                referral::AddressANVs::iterator result,
                referral::AddressANVs::iterator a,
                referral::AddressANVs::iterator b,
                referral::AddressANVs::iterator c)
        {
            if (LegacyAnvCmp(*a, *b)) {
                if (LegacyAnvCmp(*b, *c)) {
                    std::iter_swap(result, b);
                }
                else if (LegacyAnvCmp(*a, *c)) {
                    std::iter_swap(result, c);
                } else {
                    std::iter_swap(result, a);
                }
            }
            else if (LegacyAnvCmp(*a, *c)) {
                std::iter_swap(result, a);
            } else if (LegacyAnvCmp(*b, *c)) {
                std::iter_swap(result, c);
            } else {
                std::iter_swap(result, b);
            }
        }

        referral::AddressANVs::iterator PartitionAroundPivot(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last,
                referral::AddressANVs::iterator pivot)
        {
            while (true)
            {
                while (LegacyAnvCmp(*first, *pivot)) {
                    ++first;
                }

                --last;

                while (LegacyAnvCmp(*pivot, *last)) {
                    --last;
                }

                if (!(first < last)) {
                    return first;
                }

                std::iter_swap(first, last);
                ++first;
            }
        }

        referral::AddressANVs::iterator PartitionPivot(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last)
        {
            auto m = first + (last - first) / 2;
            MoveMedian(first, first + 1, m, last - 1);
            return PartitionAroundPivot(first + 1, last, first);
        }

        void IntroSort(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last,
                int limit)
        {
            while (last - first > int(16)) {
                if (limit == 0) {
                    std::partial_sort(first, last, last, LegacyAnvCmp);
                    return;
                }

                --limit;

                auto cut = PartitionPivot(first, last);
                IntroSort(cut, last, limit);
                last = cut;
            }
        }

        void LinearInsert(referral::AddressANVs::iterator last)
        {
            const auto val = *last;
            auto next = last;
            --next;

            while (LegacyAnvCmp(val, *next)) {
                *last = std::move(*next);
                last = next;
                --next;
            }
            *last = val;
        }

        void InsertionSortInner(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last)
        {
            if (first == last) {
                return;
            }

            for (auto i = first + 1; i != last; ++i) {
                if (LegacyAnvCmp(*i, *first)) {
                    auto val = std::move(*i);
                    std::move_backward(first, i, i + 1);
                    *first = std::move(val);
                } else {
                    LinearInsert(i);
                }
            }
        }

        void InsertionSort(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last)
        {
            if (last - first > int(16))
            {
                InsertionSortInner(first, first + int(16));
                for (auto i = first + int(16); i != last; ++i) {
                    LinearInsert(i);
                }
            } else {
                InsertionSortInner(first, last);
            }
        }

        size_t lg(size_t n)
        {
            return sizeof(size_t) * __CHAR_BIT__ - 1 - __builtin_clzll(n);
        }

        /**
         * A GCC compatible Sort algorithm used prior to 16000. The LegacyAnvCmp was
         * defective because it did not compare addresses when the ANV was the
         * same. A GCC compatible sort is implemented here so that the algorithm has
         * the same expected order for entries with the same ANV but different
         * address.
         */
        void LegacySort(
                referral::AddressANVs::iterator first,
                referral::AddressANVs::iterator last)
        {
            if(first == last) {
                return;
            }

            IntroSort(first, last, lg(last - first) * 2);
            InsertionSort(first, last);
        }
    }

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
    AnvDistribution::AnvDistribution(int height, referral::AddressANVs anvs) :
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

        /**
         * Prior to block 16000 the sort algorithm was defective because of the
         * comparator. Use legacy sort for old blocks and new sort after 16000
         */
        if(height < 16000) {
            LegacySort(std::begin(anvs), std::end(anvs));
        } else {
            std::sort(std::begin(anvs), std::end(anvs),
                    [](const referral::AddressANV& a, const referral::AddressANV& b) {
                        if(a.anv == b.anv) {
                            return a.address < b.address;
                        }
                        return a.anv < b.anv;
                    });
        }

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

    WalletSelector::WalletSelector(int height, const referral::AddressANVs& anvs) :
        m_distribution{height, anvs} {}

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

    referral::ConfirmedAddresses SelectConfirmedAddresses(
            const referral::ReferralsViewDB& db,
            uint256 hash,
            const uint160& genesis_address,
            size_t n,
            int max_outstanding_invites)
    {
        assert(n > 0);
        assert(max_outstanding_invites > 0);

        auto requested = n;

        const auto total = db.GetTotalConfirmations();
        auto max_tmp = std::max(static_cast<int>(n), static_cast<int>(total) / 10);
        auto max_tries = std::min(max_tmp, static_cast<int>(total));
        assert(total > 0);

        referral::ConfirmedAddresses addresses;

        while(n-- && max_tries--) {
            const auto selected_idx = SipHashUint256(0, 0, hash) % total;
            const auto sampled = db.GetConfirmation(selected_idx);

            if(!sampled) {
                return {};
            }

            if(sampled->invites > max_outstanding_invites) {
                n++;
            } else if(sampled->address == genesis_address) {
                n++;
            } else {
                addresses.push_back(*sampled);
            }

            CHashWriter hasher{SER_DISK, CLIENT_VERSION};
            hasher << hash << sampled->address;
            hash = hasher.GetHash();
        }

        assert(addresses.size() <= requested);
        return addresses;
    }

} // namespace pog
