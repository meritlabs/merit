// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp
// Copyright (c) 2017-2017 The Merit Foundation developers

#ifndef MERIT_CUCKOO_CUCKOO_H
#define MERIT_CUCKOO_CUCKOO_H

#include "uint256.h"

#include <set>
#include <vector>

enum verify_code {
  POW_OK,
  POW_HEADER_LENGTH,
  POW_TOO_BIG,
  POW_TOO_SMALL,
  POW_NON_MATCHING,
  POW_BRANCH,
  POW_DEAD_END,
  POW_SHORT_CYCLE
};

const char* errstr[] = {
  "OK",
  "wrong header length",
  "nonce too big",
  "nonces not ascending",
  "endpoints don't match up",
  "branch in cycle",
  "cycle dead ends",
  "cycle too short"};

// Find proofsize-length cuckoo cycle in random graph
bool FindCycle(const uint256& hash, unsigned int headerNonce, std::set<uint32_t>& cycle, uint8_t proofsize, uint8_t ratio);

// verify that cycle is valid in block hash generated graph
int VerifyCycle(const uint256& hash, unsigned int headerNonce, std::vector<uint32_t>& cycle, const uint8_t proofsize);

#endif // MERIT_CUCKOO_CUCKOO_H
