// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_REWARD_H
#define MERIT_POG_REWARD_H

#include "refdb.h"
#include "consensus/params.h"

namespace pog
{
    struct RewardsAmount
    {
        CAmount mining = 0;
        CAmount ambassador = 0;
    };

    struct AmbassadorReward
    {
        char address_type;
        referral::Address address;
        CAmount amount;
    };

    using Rewards = std::vector<AmbassadorReward>;

    struct AmbassadorLottery
    {
        Rewards winners;
        CAmount remainder;
    };

    AmbassadorLottery RewardAmbassadors(
            int height,
            const referral::AddressANVs& winners, CAmount total);

    struct InviteReward
    {
        char address_type;
        referral::Address address;
        CAmount invites;
    };

    struct InviteLotteryParams
    {
        int total_winners;
        int total_invites;
    };

    InviteLotteryParams ComputeInviteLotteryParams(
            int height,
            const Consensus::Params& params);

    using InviteRewards = std::vector<InviteReward>;
    InviteRewards RewardInvites(
            const referral::ConfirmedAddresses& winners,
            const InviteLotteryParams& params);

} // namespace pog

#endif //MERIT_POG_REWARD_H
