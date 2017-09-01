// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/anv.h"

#include <stack>

namespace pog 
{
    using wallet_stack = std::stack<uint256>;
    using wallet_ids = std::vector<uint256>;

    uint64_t GetWalletBalance(const uint256& )
    {
        //TODO: make work
        return 0;
    }

    wallet_ids GetReferredIds(const uint256& wallet_id)
    {
        //TODO: make work
        return {};
    }

    /**
     * Aggregate Network Value is computed by walking the referral tree down from the 
     * wallet_id specified and aggregating each child's ANV.
     *
     * This is the naive version that computes whole tree every time. We need an 
     * accumulated version instead where ANV is cached and gets updated for every
     * mined transaction.
     */
    uint64_t ComputeANV(uint256 top_wallet_id)
    {
        wallet_stack wallets;
        wallets.push(top_wallet_id);

        uint64_t anv = 0;
        while(!wallets.empty())
        {
            //aggregate balance
            const auto& wallet_id = wallets.top();
            anv += GetWalletBalance(wallet_id);
            wallets.pop();

            auto children = GetReferredIds(wallet_id);
            for(const auto& child: children)
                wallets.push(child);
        }

        return anv;
    }

} // namespace pog
