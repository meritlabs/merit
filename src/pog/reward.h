// Copyright (c) 2017-2019 The Merit Foundation
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
        int invites_created = 0;
        int invites_used = 0;
        int invites_used_fixed = 0;
        int blocks = 0;
        double mean_used = 0.0;
        double mean_used_fixed = 0.0;
    };

    double ComputeUsedInviteMean(const InviteLotteryParams& lottery);
    double ComputeUsedInviteMeanFixed(const InviteLotteryParams& lottery);

    using InviteLotteryParamsVec = std::vector<InviteLotteryParams>;

    int ComputeTotalInviteLotteryWinners(
            int height,
            const InviteLotteryParamsVec&,
            const Consensus::Params& params);

    using InviteRewards = std::vector<InviteReward>;
    InviteRewards RewardInvites(const referral::ConfirmedAddresses& winners);

} // namespace pog

#endif //MERIT_POG_REWARD_H
