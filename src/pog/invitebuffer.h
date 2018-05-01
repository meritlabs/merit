// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_INVITE_BUFFER_H
#define MERIT_POG_INVITE_BUFFER_H

#include "pog/reward.h"
#include "chain.h"
#include "sync.h"

#include <vector>

namespace pog 
{
    struct InviteStats 
    {
        int invites_created = 0;
        int invites_used = 0;
        bool is_set = false;
    };

    class InviteBuffer
    {
        public:
            InviteBuffer(const CChain& c);
            InviteStats get(int height, const Consensus::Params& p) const;

        private:
            bool get(int adjusted_height, InviteStats& s) const;
            void insert(int adjusted_height, const InviteStats& s) const;

        private:
            mutable std::vector<InviteStats> stats;
            mutable CCriticalSection cs;
            const CChain& chain;
    };

} // namespace pog

#endif //MERIT_POG_INVITE_BUFFER_H
