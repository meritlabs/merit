// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog3/reward.h"

#include "pog/wrs.h"

#include <algorithm>
#include <numeric>

namespace pog3
{
    namespace
    {
        const int INVITES_PER_WINNER = 1;
    }

    double LogCGS(const Entrant& e) 
    {
        return std::log1p(e.cgs);
    }

    double TotalCgs(const Entrants& winners)
    {
        return std::accumulate(std::begin(winners), std::end(winners), double{0.0},
                [](double acc, const Entrant& e)
                {
                    return acc + LogCGS(e);
                });
    }

    CAmount ProportionalRewards(pog::Rewards& rewards, CAmount total_reward_0, const Entrants& winners) {
        auto total_cgs = TotalCgs(winners);
        double total_reward = total_reward_0;

        pog::Rewards unfiltered_rewards;
        unfiltered_rewards.resize(winners.size());
        std::transform(std::begin(winners), std::end(winners), std::back_inserter(unfiltered_rewards),
                [total_reward, total_cgs](const Entrant& v)
                {
                    double percent = LogCGS(v) / total_cgs;
                    double reward = total_reward * percent;
                    CAmount floor_reward = std::floor(reward);

                    return pog::AmbassadorReward{
                        v.address_type,
                        v.address,
                        floor_reward};
                });

        rewards.reserve(unfiltered_rewards.size());
        std::copy_if(std::begin(unfiltered_rewards), std::end(unfiltered_rewards),
                std::back_inserter(rewards),
                [](const pog::AmbassadorReward& reward) {
                    return reward.amount > 0;
                });

        return 
            std::accumulate(std::begin(rewards), std::end(rewards), CAmount{0},
                    [](CAmount acc, const pog::AmbassadorReward& reward)
                    {
                        return acc + reward.amount;
                    });
    }


    pog::AmbassadorLottery RewardAmbassadors(
            int height,
            const Entrants& winners,
            CAmount total_reward)
    {
        pog::Rewards rewards;
        auto total_rewarded = ProportionalRewards(rewards, total_reward, winners);

        assert(total_rewarded <= total_reward);

        auto remainder = total_reward - total_rewarded;

        assert(remainder >= 0);
        assert(remainder <= total_reward);

        return {rewards, remainder};
    }

    int ComputeTotalInviteLotteryWinners(
            const pog::InviteLotteryParamsVec& lottery_points,
            const Consensus::Params& params)
    {
        assert(lottery_points.size() == 2);

        const auto& block1 = lottery_points[0];
        const auto& block2 = lottery_points[1];

        int min_total_winners = 0;

        //block1 is a weighted sum based on the imp_weights array. block1.blocks
        //is divided by the number of weights so we multiply that back here to 
        //get the correct number of blocks
        const auto min_miner_invites = block1.blocks / params.imp_miner_reward_for_every_x_blocks;
        const auto min_lottery_invites = block1.blocks / params.imp_min_one_invite_for_every_x_blocks;
        const auto min_invites = min_miner_invites + min_lottery_invites;

        LogPrint(BCLog::POG, "Invites used: %d created: %d period: %d used per block: %d min %d\n",
                block1.invites_used_fixed,
                block1.invites_created,
                block1.blocks,
                block1.mean_used_fixed,
                min_invites);


        if(block1.invites_created < min_invites) {
            min_total_winners = block1.invites_used_fixed + min_lottery_invites;
        }

        const double mean_diff = block1.mean_used_fixed - block2.mean_used_fixed;

        //Assume we need more or less than what was used before.
        //This allows invites to grow or shrink exponentially.
        const int change = mean_diff >= 0 ?
            std::ceil(mean_diff) : 
            std::floor(mean_diff);

        LogPrint(
                BCLog::POG,
                "Mean Diff: %d  change: %d b2: %d b1: %d min_total_winners:  %d\n",
                mean_diff,
                change,
                block1.mean_used_fixed,
                block2.mean_used_fixed,
                min_total_winners);

        const int total_winners = std::max(
                min_total_winners,
                static_cast<int>(std::floor(block1.mean_used_fixed) + change));

        assert(total_winners >= 0);
        return total_winners;
    }

    pog::InviteRewards RewardInvites(const referral::ConfirmedAddresses& winners)
    {
        assert(winners.size() >= 0);

        pog::InviteRewards rewards(winners.size());
        std::transform(winners.begin(), winners.end(), rewards.begin(),
                [](const referral::ConfirmedAddress& winner) {
                    return pog::InviteReward {
                        winner.address_type,
                        winner.address,
                        INVITES_PER_WINNER
                    };
                });

        assert(rewards.size() == winners.size());
        return rewards;
    }

} // namespace pog3
