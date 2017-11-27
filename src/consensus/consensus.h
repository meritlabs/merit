// Copyright (c) 2014-2017 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_CONSENSUS_CONSENSUS_H
#define MERIT_CONSENSUS_CONSENSUS_H

#include <stdlib.h>
#include <stdint.h>
#include <limits>

/** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;
/** The maximum allowed size for a serialized block transactions, in bytes */
static const unsigned int MAX_TRANSACTIONS_SERIALIZED_SIZE_SHARE = 90;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;

/** Maximum value for cuckoo miner diffucuty - block.nEdgesRatio value */
static const uint16_t MAX_CUCKOO_DIFFICULTY = std::numeric_limits<uint16_t>::max();
/** Minimum value for cuckoo miner diffucuty - block.nEdgesRatio value */
static const uint16_t MIN_CUCKOO_DIFFICULTY = MAX_CUCKOO_DIFFICULTY * .9;

/** Minimum number of edge bits for cuckoo miner - block.nEdgesBits value */
static const uint16_t MIN_EDGE_BITS = 16;
/** Maximum number of edge bits for cuckoo miner - block.nEdgesBits value */
static const uint16_t MAX_EDGE_BITS = 30;

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
enum {
    /* Interpret sequence numbers as relative lock-time constraints. */
    LOCKTIME_VERIFY_SEQUENCE = (1 << 0),

    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

#endif // MERIT_CONSENSUS_CONSENSUS_H
