// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp
// Copyright (c) 2017-2017 The Merit Foundation developers

#ifndef MERIT_CUCKOO_CUCKOO_H
#define MERIT_CUCKOO_CUCKOO_H

#include "crypto/siphash.h"
#include <assert.h>
#include <stdint.h> // for types uint32_t,uint64_t

// proof-of-work parameters
#ifndef EDGEBITS
// the main parameter is the 2-log of the graph size,
// which is the size in bits of the node identifiers
#define EDGEBITS 19
#endif

// number of edges
#define NEDGES ((uint32_t)1 << EDGEBITS)
// used to mask siphash output
#define EDGEMASK ((uint32_t)NEDGES - 1)

// assume EDGEBITS < 31
#define NNODES (2 * NEDGES)

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

const char* errstr[] = {
    "OK",
    "wrong header length",
    "nonce too big",
    "nonces not ascending",
    "endpoints don't match up",
    "branch in cycle",
    "cycle dead ends",
    "cycle too short"};

const char* nonceToHeader(const char* header, const uint32_t headerlen, const uint32_t nonce);

// convenience function for extracting siphash keys from header
void SetKeys(const char* header, const uint32_t headerlen, siphash_keys* keys);

class CuckooCtx
{
public:
    siphash_keys m_Keys;
    uint32_t m_difficulty;
    uint32_t* m_cuckoo;

    CuckooCtx(const char* header, const uint32_t headerlen, const uint32_t nonce, uint32_t difficulty)
    {
        SetKeys(nonceToHeader(header, headerlen, nonce), headerlen, &m_Keys);

        m_difficulty = difficulty;
        m_cuckoo = (uint32_t*)calloc(1 + NNODES, sizeof(uint32_t));

        assert(m_cuckoo != 0);
    }

    ~CuckooCtx()
    {
        free(m_cuckoo);
    }
};

// Find proofsize-length cuckoo cycle in random graph
bool FindCycle(CuckooCtx* ctx, std::set<uint32_t>& cycle, uint8_t proofsize);

// verify that nonces are ascending and form a cycle in header-generated graph
int VerifyCycle(std::vector<uint32_t>& nonces, siphash_keys* keys, const uint8_t proofsize);

#endif // MERIT_CUCKOO_CUCKOO_H
