// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CUCKOO_MINER_H
#define MERIT_CUCKOO_MINER_H

#include "uint256.h"
#include "consensus/params.h"
#include <set>

namespace cuckoo {
    /** Find cycle for block that satisfies the proof-of-work requirement specified by block hash and nonce */
    bool FindProofOfWork(uint256 hash, int nonce, unsigned int nBits, std::set<uint32_t>& cycle, const Consensus::Params& params);

    /** Check that provided cycle satisfies the proof-of-work requirement specified by block hash and nonce */
    bool VerifyProofOfWork(uint256 hash, int nonce, unsigned int nBits, const std::set<uint32_t>& cycle, const Consensus::Params& params);
}

#endif // MERIT_CUCKOO_MINER_H
