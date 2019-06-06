// Copyright (c) 2017-2019 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CONSENSUS_REF_VERIFY_H
#define MERIT_CONSENSUS_REF_VERIFY_H

class CValidationState;

namespace Consensus
{
    struct Params;
}

namespace referral
{
    class Referral;
    class ReferralsViewCache;

    /** Referral validation functions */

    /** Context-independent validity checks */
    bool CheckReferral(
            const Referral& referral,
            bool normalize_alias,
            CValidationState& state);

} //namespace referral

#endif // MERIT_CONSENSUS_REF_VERIFY_H
