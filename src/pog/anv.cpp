// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/anv.h"
#include <stack>

namespace pog 
{
    /**
     * This version simply pulls the ANV from the DB. ReferralsViewDB::UpdateANV
     * incrementally updates an ANV for a key and all parents.
     */
    MaybeANV ComputeANV(const CKeyID& key_id, const ReferralsViewDB& db)
    {
        return db.GetANV(key_id);
    }

    KeyANVs GetAllANVs(const ReferralsViewDB& db)
    {
        return db.GetAllANVs();
    }

    KeyANVs GetANVs(const KeyIDs& keys, const ReferralsViewDB& db)
    {
        KeyANVs r;
        r.reserve(keys.size());

        for(const auto& k : keys) {
            if(auto maybe_anv = ComputeANV(k, db))
                r.push_back({k, *maybe_anv});
        }

        assert(r.size() <= keys.size());
        return r;
    }

} // namespace pog
