// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG3_CGS_H
#define MERIT_POG3_CGS_H

#include "primitives/referral.h"
#include "consensus/params.h"
#include "referrals.h"
#include "pog/wrs.h"
#include "coins.h"

#include <vector>
#include <boost/optional.hpp>

namespace pog3
{

    struct Entrant
    {
        char address_type;
        referral::Address address;
        CAmount balance;
        CAmount aged_balance;
        CAmount cgs;
        int beacon_height;
        size_t children;
        size_t network_size;
    };

    using MaybeEntrant = boost::optional<Entrant>;

    using ContributionAmount = pog::BigFloat;
    struct Contribution
    {
        ContributionAmount value = 0.0;
    };

    struct SubtreeContribution
    {
        ContributionAmount value = 0.0;
        size_t tree_size = 0;
    };

    //Aged and non-aged balance.
    using BalancePair = std::pair<CAmount, CAmount>;
    using BalancePairs = std::vector<BalancePair>;

    struct Coin
    {
        Coin(int h, CAmount a) : height{h}, amount{a} {}
        int height;
        CAmount amount;
    };

    using Coins = std::vector<Coin>;
    struct AddressBalance {
    };

    using AddressBalances = std::map<referral::Address, AddressBalance>;

    using AddressPair = std::pair<char, referral::Address>;
    using Addresses = std::vector<referral::Address>;
    using Children = Addresses;

    struct CachedEntrant
    {
        referral::Address address;
        char address_type;
        Coins coins;
        BalancePair balances;
        Contribution contribution;
        int height;
        Children children;
    };

    struct CGSContext
    {
        int tip_height;
        int coin_maturity;
        int new_coin_maturity;
        SubtreeContribution tree_contribution; 

        std::vector<CachedEntrant> entrants;
        std::map<referral::Address, size_t> entrant_idx;

        std::map<referral::Address, SubtreeContribution> subtree_contribution;
        double B;
        double S;

        CachedEntrant& AddEntrant(
                char address_type,
                const referral::Address& address,
                int height,
                const Children& children);

        CachedEntrant& GetEntrant(const referral::Address&);
        const CachedEntrant& GetEntrant(const referral::Address&) const;

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
            const CachedEntrant& entrant,
            referral::ReferralsViewCache& db);

    void TestChain();
    void SetupCgsThreadPool(size_t threads);

    CAmount GetAmbassadorMinumumStake(int height, const Consensus::Params& consensus_params);

} // namespace pog3

#endif //MERIT_POG2_ANV_H
