// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp
// Copyright (c) 2017-2017 The Merit Foundation developers

#ifndef MERIT_CUCKOO_MEAN_CUCKOO_H
#define MERIT_CUCKOO_MEAN_CUCKOO_H

#include "uint256.h"

#include <set>
#include <vector>

// Find proofsize-length cuckoo cycle in random graph
bool FindCycleAdvanced(const uint256& hash, uint8_t edgeBits, uint8_t proofSize, std::set<uint32_t>& cycle, uint8_t nThreads = 1);

#endif // MERIT_CUCKOO_MEAN_CUCKOO_H
