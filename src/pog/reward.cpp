// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/reward.h"

#include <algorithm>
#include <numeric>

namespace pog 
{

    AmbassadorLottery RewardAmbassadors(
            const referral::AddressANVs& winners,
            CAmount total_reward)
    {
        CAmount total_anv = 
            std::accumulate(std::begin(winners), std::end(winners), CAmount{0}, 
                    [](CAmount acc, const referral::AddressANV& v) 
                    { 
                        return acc + v.anv;
                    });

        Rewards rewards(winners.size());
        std::transform(std::begin(winners), std::end(winners), std::begin(rewards),
                [total_reward, total_anv](const referral::AddressANV& v) 
                { 
                    auto percent = (v.anv*1000) / total_anv;
                    CAmount reward = (total_reward * percent) / 1000;
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

} // namespace pog
