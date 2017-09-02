// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_SELECT_H
#define MERIT_POG_SELECT_H

#include "hash.h"
#include <vector>
#include <map>

namespace pog 
{
    struct WalletAnv
    {
        uint256 wallet;
        uint64_t anv;
    };

    using WalletAnvs = std::vector<WalletAnv>;
    using InvertedAnvs = WalletAnvs;
    using WalletToAnv = std::map<uint256, WalletAnv>;

    class AnvDistribution
    {
        public:
            AnvDistribution(WalletAnvs anvs);
            const WalletAnv& Sample(const uint256& hash) const;
            size_t Size() const;

        private:
            InvertedAnvs m_inverted;
            WalletToAnv m_anvs;
            uint64_t m_max_anv = 0;
    };

    class WalletSelector
    {
        public:
            WalletSelector(const WalletAnvs& anvs);

            WalletAnvs Select(uint256 hash, size_t n) const;
        private:
            AnvDistribution m_distribution;
    };

} // namespace pog

#endif //MERIT_POG_SELECT_H
