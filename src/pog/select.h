// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_SELECT_H
#define MERIT_POG_SELECT_H

#include "hash.h"
#include "amount.h"
#include "pog/anv.h"

#include <map>

namespace pog 
{
    using InvertedAnvs = KeyANVs;
    using WalletToAnv = std::map<CKeyID, KeyANV>;

    class AnvDistribution
    {
        public:
            AnvDistribution(KeyANVs anvs);
            const KeyANV& Sample(const uint256& hash) const;
            size_t Size() const;

        private:
            InvertedAnvs m_inverted;
            WalletToAnv m_anvs;
            CAmount m_max_anv = 0;
    };

    class WalletSelector
    {
        public:
            WalletSelector(const KeyANVs& anvs);

            KeyANVs Select(uint256 hash, size_t n) const;
        private:
            AnvDistribution m_distribution;
    };

} // namespace pog

#endif //MERIT_POG_SELECT_H
