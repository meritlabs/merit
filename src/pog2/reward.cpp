// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog2/reward.h"

#include "pog/wrs.h"

#include <algorithm>
#include <numeric>

namespace pog2
{
    namespace
    {
        const int INVITES_PER_WINNER = 1;
    }

    pog::BigFloat LogCGS(const Entrant& e) 
    {
        return boost::multiprecision::log(pog::BigFloat{1.0} + e.cgs);
    }

    pog::BigFloat TotalCgs(const Entrants& winners)
    {
        return std::accumulate(std::begin(winners), std::end(winners), pog::BigFloat{0},
                [](pog::BigFloat acc, const Entrant& e)
                {
                    return acc + LogCGS(e);
                });
    }

    CAmount ProportionalRewards(pog::Rewards& rewards, CAmount total_reward_0, const Entrants& winners) {
        auto total_cgs = TotalCgs(winners);
        pog::BigFloat total_reward = total_reward_0;

        pog::Rewards unfiltered_rewards;
        unfiltered_rewards.resize(winners.size());
        std::transform(std::begin(winners), std::end(winners), std::back_inserter(unfiltered_rewards),
                [total_reward, total_cgs](const Entrant& v)
                {
                    pog::BigFloat percent = LogCGS(v) / total_cgs;
                    pog::BigFloat reward = total_reward * percent;
                    auto floor_reward = reward.convert_to<CAmount>();

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

        LogPrint(BCLog::VALIDATION, "Invites used: %d created: %d period: %d used per block: %d\n",
                block1.invites_used,
                block1.invites_created,
                params.daedalus_block_window,
                block1.mean_used);


        int min_total_winners = 0;
        if(block1.invites_created <= (block1.blocks / params.imp_miner_reward_for_every_x_blocks)) {
            min_total_winners = block1.invites_used + 
                (block1.blocks / params.imp_min_one_invite_for_every_x_blocks);
        }

        const double mean_diff = block1.mean_used - block2.mean_used;

        //Assume we need more or less than what was used before.
        //This allows invites to grow or shrink exponentially.
        const int change = mean_diff >= 0 ?
            std::ceil(mean_diff) : 
            std::floor(mean_diff);

        const int total_winners = std::max(
                min_total_winners,
                static_cast<int>(std::floor(block1.mean_used) + change));

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

} // namespace pog2
