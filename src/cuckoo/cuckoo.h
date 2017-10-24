// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp
// Copyright (c) 2017-2017 The Merit Foundation developers

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

// convenience function for extracting siphash keys from header
void setKeys(const char* header, const uint32_t headerlen, siphash_keys* keys)
{
    char hdrkey[32];
    // SHA256((unsigned char *)header, headerlen, (unsigned char *)hdrkey);
    blake2b((void*)hdrkey, sizeof(hdrkey), (const void*)header, headerlen, 0, 0);

    keys->k0 = htole64(((uint64_t*)hdrkey)[0]);
    keys->k1 = htole64(((uint64_t*)hdrkey)[1]);
}

// generate edge endpoint in cuckoo graph
uint32_t sipnode(CSipHasher* hasher, uint32_t mask, uint32_t nonce, uint32_t uorv)
{
    auto node = CSipHasher(*hasher).Write(2 * nonce + uorv).Finalize() & mask;

    return node << 1 | uorv;
}

// Find proofsize-length cuckoo cycle in random graph
bool FindCycle(const uint256& hash, uint8_t nodesBits, uint8_t edgesRatio, uint8_t proofSize, std::set<uint32_t>& cycle);

// verify that cycle is valid in block hash generated graph
int VerifyCycle(const uint256& hash, uint8_t nodesBits, uint8_t proofSize, const std::vector<uint32_t>& cycle);

#endif // MERIT_CUCKOO_CUCKOO_H
