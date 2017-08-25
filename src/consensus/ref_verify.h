// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_REF_VERIFY_H
#define BITCOIN_CONSENSUS_REF_VERIFY_H

#include <stdint.h>
#include <vector>

class Referral;
class CValidationState;

namespace Consensus 
{
    // Context Independent referral validity checks
    bool CheckReferral(const Referral& tx, CValidationState& state);
}

#endif // BITCOIN_CONSENSUS_REF_VERIFY_H
