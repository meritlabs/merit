// Copyright (c) 2017-2020 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG3_REWARD_H
#define MERIT_POG3_REWARD_H

#include "refdb.h"
#include "pog/reward.h"
#include "pog3/cgs.h"
#include "consensus/params.h"

namespace pog3
{
    pog::AmbassadorLottery RewardAmbassadors(
            int height,
            const Entrants& winners,
            CAmount total);

    double ComputeUsedInviteMean(const pog::InviteLotteryParams& lottery);

    int ComputeTotalInviteLotteryWinners(
            const pog::InviteLotteryParamsVec&,
            const Consensus::Params& params);

    pog::InviteRewards RewardInvites(const referral::ConfirmedAddresses& winners);

} // namespace pog3

#endif //MERIT_POG2_REWARD_H
