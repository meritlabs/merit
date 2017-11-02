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
uint64_t siphash24(const siphash_keys* keys, const uint64_t nonce)
{
    uint64_t v0 = keys->k0 ^ 0x736f6d6570736575ULL,
             v1 = keys->k1 ^ 0x646f72616e646f6dULL,
             v2 = keys->k0 ^ 0x6c7967656e657261ULL,
             v3 = keys->k1 ^ 0x7465646279746573ULL ^ nonce;
    SIPROUND;
    SIPROUND;
    v0 ^= nonce;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return (v0 ^ v1) ^ (v2 ^ v3);
}


// convenience function for extracting siphash keys from header
void setKeys(const char* header, const uint32_t headerlen, siphash_keys* keys)
{
    char hdrkey[32];
    // SHA256((unsigned char *)header, headerlen, (unsigned char *)hdrkey);
    blake2b((void*)hdrkey, sizeof(hdrkey), (const void*)header, headerlen, 0, 0);

    printf("header: %s, len: %d\n", header, headerlen);
    printf("hdrkey: %llx\n", ((uint64_t *)hdrkey)[0]);

    keys->k0 = htole64(((uint64_t*)hdrkey)[0]);
    keys->k1 = htole64(((uint64_t*)hdrkey)[1]);
}

// generate edge endpoint in cuckoo graph without partition bit
uint32_t _sipnode(const siphash_keys* keys, uint32_t mask, uint32_t nonce, uint32_t uorv)
{
    return siphash24(keys, 2 * nonce + uorv) & mask;
}

// generate edge endpoint in cuckoo graph without partition bit
uint32_t _sipnode(const CSipHasher* hasher, uint32_t mask, uint32_t nonce, uint32_t uorv)
{
    return CSipHasher(*hasher).Write(2 * nonce + uorv).Finalize() & mask;
}

// generate edge endpoint in cuckoo graph
uint32_t sipnode(const CSipHasher* hasher, uint32_t mask, uint32_t nonce, uint32_t uorv)
{
    auto node = _sipnode(hasher, mask, nonce, uorv);

    return node << 1 | uorv;
}

uint32_t sipnode(const siphash_keys* keys, uint32_t mask, uint32_t nonce, uint32_t uorv)
{
    auto node = _sipnode(keys, mask, nonce, uorv);

    return node << 1 | uorv;
}

// Find proofsize-length cuckoo cycle in random graph
bool FindCycle(const uint256& hash, uint8_t nodesBits, uint8_t edgesRatio, uint8_t proofSize, std::set<uint32_t>& cycle);

// verify that cycle is valid in block hash generated graph
int VerifyCycle(const uint256& hash, uint8_t nodesBits, uint8_t proofSize, const std::vector<uint32_t>& cycle);


#endif // MERIT_CUCKOO_CUCKOO_H
