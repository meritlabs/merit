/*
 * Cuckoo Cycle, a memory-hard proof-of-work
 * Copyright (c) 2013-2018 John Tromp
 * Copyright (c) 2017-2018 The Merit Foundation developers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the The FAIR MINING License and, alternatively,
 * GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See src/cuckoo/LICENSE.md for more details.
 **/

#ifndef MERIT_CUCKOO_CUCKOO_H
#define MERIT_CUCKOO_CUCKOO_H

#include "crypto/blake2/blake2.h"
#include "hash.h"
#include "uint256.h"

#include <set>
#include <vector>

#define MAXPATHLEN 8192

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

extern const char* errstr[];

// siphash uses a pair of 64-bit keys,
typedef struct {
    uint64_t k0;
    uint64_t k1;
} siphash_keys;

#define ROTL(x,b) (uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )
#define SIPROUND \
  do { \
    v0 += v1; v2 += v3; v1 = ROTL(v1,13); \
    v3 = ROTL(v3,16); v1 ^= v0; v3 ^= v2; \
    v0 = ROTL(v0,32); v2 += v1; v0 += v3; \
    v1 = ROTL(v1,17);   v3 = ROTL(v3,21); \
    v1 ^= v2; v3 ^= v0; v2 = ROTL(v2,32); \
  } while(0)

// SipHash-2-4 specialized to precomputed key and 8 byte nonces
uint64_t siphash24(const siphash_keys* keys, const uint64_t nonce);

// convenience function for extracting siphash keys from header
void setKeys(const char* header, const uint32_t headerlen, siphash_keys* keys);

// generate edge endpoint in cuckoo graph without partition bit
uint32_t _sipnode(const siphash_keys* keys, uint32_t mask, uint32_t nonce, uint32_t uorv);

// generate edge endpoint in cuckoo graph without partition bit
uint32_t _sipnode(const CSipHasher* hasher, uint32_t mask, uint32_t nonce, uint32_t uorv);

// generate edge endpoint in cuckoo graph
uint32_t sipnode(const CSipHasher* hasher, uint32_t mask, uint32_t nonce, uint32_t uorv);

uint32_t sipnode(const siphash_keys* keys, uint32_t mask, uint32_t nonce, uint32_t uorv);

// Find proofsize-length cuckoo cycle in random graph
bool FindCycle(const uint256& hash, uint8_t edgeBits, uint8_t proofSize, std::set<uint32_t>& cycle);

// verify that cycle is valid in block hash generated graph
int VerifyCycle(const uint256& hash, uint8_t edgeBits, uint8_t proofSize, const std::vector<uint32_t>& cycle);


#endif // MERIT_CUCKOO_CUCKOO_H
