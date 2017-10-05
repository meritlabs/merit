// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CUCKOO_MINER_H
#define MERIT_CUCKOO_MINER_H

#include "uint256.h"
#include <set>

namespace cuckoo {
    /** Find cycles for block hash satisfies the proof-of-work requirement specified by block hash and nonce */
    bool FindProofOfWork(uint256 hash, int nonce, std::set<uint32_t>& cycle);

    bool VerifyProofOfWork(uint256, int nonce, const std::set<uint32_t>& cycle);
}

#endif // MERIT_CUCKOO_MINER_H
