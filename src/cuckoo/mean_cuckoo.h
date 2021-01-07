/*
 * Cuckoo Cycle, a memory-hard proof-of-work
 * Copyright (c) 2013-2018 John Tromp
 * Copyright (c) 2017-2021 The Merit Foundation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the The FAIR MINING License and, alternatively,
 * GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See src/cuckoo/LICENSE.md for more details.
 **/

#ifndef MERIT_CUCKOO_MEAN_CUCKOO_H
#define MERIT_CUCKOO_MEAN_CUCKOO_H

#include "uint256.h"
#include "ctpl/ctpl.h"

#include <set>
#include <vector>

// Find proofsize-length cuckoo cycle in random graph
bool FindCycleAdvanced(
    const uint256& hash,
    uint8_t edgeBits,
    uint8_t proofSize,
    std::set<uint32_t>& cycle,
    size_t threads_number,
    ctpl::thread_pool&);

#endif // MERIT_CUCKOO_MEAN_CUCKOO_H
