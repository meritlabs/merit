// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_ANV2_H
#define MERIT_POG_ANV2_H

#include "primitives/referral.h"
#include "consensus/params.h"
#include "refdb.h"
#include "coins.h"

#include <vector>

namespace pog2
{
    struct Entrant
    {
        char address_type;
        referral::Address address;
        CAmount cgs;
        double level;
        int children;
        int network_size;
    };

    using Entrants = std::vector<Entrant>;

    void GetAllRewardableEntrants(
            const referral::ReferralsViewDB&,
            const Consensus::Params&,
            int height,
            Entrants&);

    Entrant ComputeCGS(
            int height,
            char address_type,
            const referral::Address& address,
            const referral::ReferralsViewDB& db);

} // namespace pog2

#endif //MERIT_POG2_ANV_H
