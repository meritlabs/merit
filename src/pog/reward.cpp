// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/reward.h"

#include <algorithm>
#include <numeric>

namespace pog 
{

    AmbassadorRewards RewardAmbassadors(const KeyANVs& winners, CAmount total_reward)
    {
        CAmount total_anv = 
            std::accumulate(std::begin(winners), std::end(winners), CAmount{0}, 
                    [](CAmount acc, const KeyANV& v) 
                    { 
                        return acc + v.anv;
                    });

        Rewards rewards(winners.size());
        std::transform(std::begin(winners), std::end(winners), std::begin(rewards),
                [total_reward, total_anv](const KeyANV& v) 
                { 
                    auto percent = (v.anv*100) / total_anv;
                    CAmount reward = (total_reward * percent) / 100;
                    assert(reward <= total_reward);
                    return AmbassadorReward{v.key, reward};
                });

        CAmount total_rewarded = 
            std::accumulate(std::begin(rewards), std::end(rewards), CAmount{0}, 
                    [](CAmount acc, const AmbassadorReward& reward) 
                    { 
                        return acc + reward.amount;
                    });

        assert(total_rewarded >= 0);
        assert(total_rewarded <= total_reward);

        auto remainder = total_reward - total_rewarded;

        assert(remainder >= 0);
        assert(remainder <= total_reward);

        return {rewards, remainder};
    }

} // namespace pog
