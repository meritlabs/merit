// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/anv.h"
#include <stack>

namespace pog 
{
    using KeyStack = std::stack<CKeyID>;

    uint64_t GetKeyBalance(const CKeyID& key)
    {
        //TODO: make work
        return 0;
    }

    ChildKeys GetReferredIds(const CKeyID& key, const ReferralsViewDB& db)
    {
        return db.GetChildren(key);
    }

    /**
     * Aggregate Network Value is computed by walking the referral tree down from the 
     * key_id specified and aggregating each child's ANV.
     *
     * This is the naive version that computes whole tree every time. We need an 
     * accumulated version instead where ANV is cached and gets updated for every
     * mined transaction.
     */
    uint64_t ComputeANV(const CKeyID& top_key_id, const ReferralsViewDB& db)
    {
        KeyStack keys;
        keys.push(top_key_id);

        uint64_t anv = 0;
        while(!keys.empty())
        {
            //aggregate balance
            const auto& key_id = keys.top();
            anv += GetKeyBalance(key_id);
            keys.pop();

            auto children = GetReferredIds(key_id, db);
            for(const auto& child: children)
                keys.push(child);
        }

        return anv;
    }

} // namespace pog
