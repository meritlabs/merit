// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_REWARD_H
#define MERIT_POG_REWARD_H

#include "refdb.h"

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

} // namespace pog

#endif //MERIT_POG_REWARD_H
