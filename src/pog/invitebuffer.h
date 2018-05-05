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
    struct MeanStats
    {
        int invites_created = 0;
        int invites_used = 0;
        int blocks = 0;
        double mean_used = 0.0;

        MeanStats() {}

        MeanStats(
                int created,
                int used,
                int blks,
                double mean) :
            invites_created{created},
            invites_used{used},
            blocks{blks},
            mean_used{mean} {}
    };

    struct InviteStats 
    {
        MeanStats mean_stats;
        int invites_created = 0;
        int invites_used = 0;
        bool is_set = false;
        bool mean_set = false;
    };

    class InviteBuffer
    {
        public:
            InviteBuffer(const CChain& c);
            InviteStats get(int height, const Consensus::Params& p) const;
            bool set_mean(int height, const MeanStats& mean_stats, const Consensus::Params& p);

            bool drop(int height, const Consensus::Params& p);

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
