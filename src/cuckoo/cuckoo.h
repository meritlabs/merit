// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp

#include "crypto/blake2/blake2.h"
#include "crypto/siphash.h"
#include <stdint.h> // for types uint32_t,uint64_t
#include <string.h> // for functions strlen, memset

// proof-of-work parameters
#ifndef EDGEBITS
// the main parameter is the 2-log of the graph size,
// which is the size in bits of the node identifiers
#define EDGEBITS 19
#endif
#ifndef PROOFSIZE
// the next most important parameter is the (even) length
// of the cycle to be found. a minimum of 12 is recommended
#define PROOFSIZE 42
#endif

// number of edges
#define NEDGES ((uint32_t)1 << EDGEBITS)
// used to mask siphash output
#define EDGEMASK ((uint32_t)NEDGES - 1)

// generate edge endpoint in cuckoo graph without partition bit
uint32_t _sipnode(siphash_keys* keys, uint32_t nonce, u32 uorv)
{
    return siphash24(keys, 2 * nonce + uorv) & EDGEMASK;
}

// generate edge endpoint in cuckoo graph
uint32_t sipnode(siphash_keys* keys, uint32_t nonce, u32 uorv)
{
    return _sipnode(keys, nonce, uorv) << 1 | uorv;
}

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

// verify that nonces are ascending and form a cycle in header-generated graph
int VerifyCycle(std::vector<uint32_t>& nonces, siphash_keys* keys, const uint8_t proofsize)
{
    assert(nonces.size() == proofsize);

    uint32_t uvs[2 * proofsize];
    uint32_t xor0 = 0, xor1 = 0;

    for (uint32_t n = 0; n < proofsize; n++) {
        if (nonces[n] > EDGEMASK) {
            return POW_TOO_BIG;
        }

        if (n && nonces[n] <= nonces[n - 1]) {
            return POW_TOO_SMALL;
        }

        xor0 ^= uvs[2 * n] = sipnode(keys, nonces[n], 0);
        xor1 ^= uvs[2 * n + 1] = sipnode(keys, nonces[n], 1);
    }

    // matching endpoints imply zero xors
    if (xor0 | xor1) {
        return POW_NON_MATCHING;
    }

    uint32_t n = 0, i = 0, j;
    do { // follow cycle
        for (uint32_t k = j = i; (k = (k + 2) % (2 * proofsize)) != i;) {
            if (uvs[k] == uvs[i]) { // find other edge endpoint identical to one at i
                if (j != i) {       // already found one before
                    return POW_BRANCH;
                }

                j = k;
            }
        }
        if (j == i) {
            return POW_DEAD_END; // no matching endpoint
        }

        i = j ^ 1;
        n++;
    } while (i != 0); // must cycle back to start or we would have found branch

    return n == proofsize ? POW_OK : POW_SHORT_CYCLE;
}

// convenience function for extracting siphash keys from header
void SetKeys(const char* header, const uint32_t headerlen, siphash_keys* keys)
{
    char hdrkey[32];
    // SHA256((unsigned char *)header, headerlen, (unsigned char *)hdrkey);
    blake2b((void*)hdrkey, sizeof(hdrkey), (const void*)header, headerlen, 0, 0);
    setkeys(keys, hdrkey);
}
