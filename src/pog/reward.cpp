// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/reward.h"

#include <algorithm>
#include <numeric>

namespace pog
{
    const int DAY = 24 * 60 * 60;

    AmbassadorLottery RewardAmbassadors(
            int height,
            const referral::AddressANVs& winners,
            CAmount total_reward)
    {
        /**
         * Increase ANV precision on block 16000
         */
        CAmount fixed_precision = height < 16000 ? 100 : 1000;

        CAmount total_anv =
            std::accumulate(std::begin(winners), std::end(winners), CAmount{0},
                    [](CAmount acc, const referral::AddressANV& v)
                    {
                        return acc + v.anv;
                    });

        Rewards rewards(winners.size());
        std::transform(std::begin(winners), std::end(winners), std::begin(rewards),
                [total_reward, total_anv, fixed_precision](const referral::AddressANV& v)
                {
                    double percent = (v.anv*fixed_precision) / total_anv;
                    CAmount reward = (total_reward * percent) / fixed_precision;
                    assert(reward <= total_reward);
                    return AmbassadorReward{v.address_type, v.address, reward};
                });

        Rewards filtered_rewards;
        filtered_rewards.reserve(rewards.size());
        std::copy_if(std::begin(rewards), std::end(rewards),
                std::back_inserter(filtered_rewards),
                [](const AmbassadorReward& reward) {
                    return reward.amount > 0;
                });

        CAmount total_rewarded =
            std::accumulate(std::begin(filtered_rewards), std::end(filtered_rewards), CAmount{0},
                    [](CAmount acc, const AmbassadorReward& reward)
                    {
                        return acc + reward.amount;
                    });

        assert(total_rewarded >= 0);
        assert(total_rewarded <= total_reward);

        auto remainder = total_reward - total_rewarded;

        assert(remainder >= 0);
        assert(remainder <= total_reward);

        return {filtered_rewards, remainder};
    }

    InviteLotteryParams ComputeInviteLotteryParams(
            int height,
            const Consensus::Params& params)
    {
        const auto start_block = params.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].start_block;
        assert(height >= start_block);

        const auto blocks = height - start_block;
        const auto blocks_per_day = DAY / params.nPowTargetSpacing;
        const auto days = blocks / blocks_per_day;

        const auto mult = 1 + (days / 30); //increase rate every 30 days

        assert(mult > 0);
        const int invites_per_block = params.daedalus_base_invites_per_block * mult;

        const auto total_winners =
            std::min(invites_per_block, params.daedalus_max_winners_per_block);

        return { total_winners, invites_per_block};
    }

    InviteRewards RewardInvites(
            const referral::ConfirmedAddresses& winners,
            const InviteLotteryParams& params)
    {
        assert(winners.size() == params.total_winners);

        const auto invites_per_winner =
            params.total_invites /
            params.total_winners;

        assert(invites_per_winner > 0);

        InviteRewards rewards(winners.size());
        std::transform(winners.begin(), winners.end(), rewards.begin(),
                [invites_per_winner](const referral::ConfirmedAddress& winner) -> InviteReward {
                    return {
                        winner.address_type,
                        winner.address,
                        invites_per_winner
                    };
                });

        const auto remaining_invites =
            params.total_invites - (invites_per_winner * params.total_winners);

        assert(remaining_invites >= 0);

        //Give remainder to the first guy.
        rewards[0].invites += remaining_invites;

        assert(rewards.size() == winners.size());
        return rewards;
    }

} // namespace pog
