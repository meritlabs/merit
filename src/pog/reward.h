// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_REWARD_H
#define MERIT_POG_REWARD_H

#include "refdb.h"

namespace pog 
{
    struct AmbassadorReward
    {
        CKeyID key;
        CAmount amount;
    };

    using Rewards = std::vector<AmbassadorReward>;

    struct AmbassadorRewards
    {
        Rewards ambassadors;
        CAmount remainder;
    };

    AmbassadorRewards RewardAmbassadors(const KeyANVs& winners, CAmount total); 

} // namespace pog

#endif //MERIT_POG_REWARD_H
