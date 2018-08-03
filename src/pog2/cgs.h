// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_ANV2_H
#define MERIT_POG_ANV2_H

#include "primitives/referral.h"
#include "consensus/params.h"
#include "referrals.h"
#include "pog2/wrs.h"
#include "coins.h"

#include <vector>
#include <boost/optional.hpp>

namespace pog2
{

    struct Entrant
    {
        char address_type;
        referral::Address address;
        CAmount balance;
        CAmount aged_balance;
        CAmount cgs;
        CAmount sub_cgs;
        int beacon_height;
        size_t children;
        size_t network_size;
    };

    using MaybeEntrant = boost::optional<Entrant>;

    using ContributionAmount = BigFloat;
    struct Contribution
    {
        ContributionAmount value = 0.0;
        ContributionAmount sub = 0.0;
    };

    struct SubtreeContribution
    {
        ContributionAmount value = 0.0;
        ContributionAmount sub = 0.0;
        size_t tree_size = 0;
    };

    //Aged and non-aged balance.
    using BalancePair = std::pair<double, CAmount>;
    using BalancePairs = std::vector<BalancePair>;

    struct CGSContext
    {
        int tip_height;
        int coin_maturity;
        int new_coin_maturity;
        SubtreeContribution tree_contribution; 

        std::map<referral::Address, Contribution> contribution;
        std::map<referral::Address, SubtreeContribution> subtree_contribution;

        std::map<referral::Address, BalancePair> balances;
        std::map<referral::Address, Entrant> entrant_cgs;

        double B;
        double S;
    };

    using Entrants = std::vector<Entrant>;

    void GetAllRewardableEntrants(
            CGSContext& context,
            referral::ReferralsViewCache&,
            const Consensus::Params&,
            int height,
            Entrants&);


    Entrant ComputeCGS(
            CGSContext& context,
            char address_type,
            const referral::Address& address,
            referral::ReferralsViewCache& db);

    void TestChain();

} // namespace pog2

#endif //MERIT_POG2_ANV_H
