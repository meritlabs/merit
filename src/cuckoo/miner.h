// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CUCKOO_MINER_H
#define MERIT_CUCKOO_MINER_H

#include <stdint.h>
#include "uint256.h"

class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;
class ReferralsViewCache;


namespace cuckoo {
    /** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
    bool CheckProofOfWork(uint256 hash, int nonce);
}

#endif // MERIT_CUCKOO_MINER_H
