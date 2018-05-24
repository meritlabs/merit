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

#include "cuckoo.h"
#include "consensus/consensus.h"
#include "util.h"

#include <stdint.h> // for types uint32_t,uint64_t
#include <string.h> // for functions strlen, memset

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

const char* errstr[] = {
    "OK",
    "wrong header length",
    "nonce too big",
    "nonces not ascending",
    "endpoints don't match up",
    "branch in cycle",
    "cycle dead ends",
    "cycle too short"};

class CuckooCtx
{
public:
    CSipHasher* m_hasher;
    siphash_keys m_keys;
    uint32_t m_difficulty;
    uint32_t* m_cuckoo;

    CuckooCtx(const char* header, const uint32_t headerlen, uint32_t difficulty, uint32_t nodesCount)
    {
        setKeys(header, headerlen, &m_keys);
        m_hasher = new CSipHasher(m_keys.k0, m_keys.k1);

        m_difficulty = difficulty;
        m_cuckoo = (uint32_t*)calloc(1 + nodesCount, sizeof(uint32_t));

        assert(m_cuckoo != 0);
    }

    ~CuckooCtx()
    {
        free(m_cuckoo);
    }
};

int path(uint32_t* cuckoo, uint32_t u, uint32_t* us)
{
    int nu;
    for (nu = 0; u; u = cuckoo[u]) {
        if (++nu >= MAXPATHLEN) {
            LogPrintf("nu is %d\n", nu);
            while (nu-- && us[nu] != u)
                ;
            if (nu < 0)
                LogPrintf("maximum path length exceeded\n");
            else
                LogPrintf("illegal % 4d-cycle\n", MAXPATHLEN - nu);
            exit(0);
        }
        us[nu] = u;
    }
    return nu;
}

typedef std::pair<uint32_t, uint32_t> edge;

void solution(CuckooCtx* ctx, uint32_t* us, int nu, uint32_t* vs, int nv, std::set<uint32_t>& nonces, const uint32_t edgeMask)
{
    assert(nonces.empty());
    std::set<edge> cycle;

    unsigned n;
    cycle.insert(edge(*us, *vs));
    while (nu--) {
        cycle.insert(edge(us[(nu + 1) & ~1], us[nu | 1])); // u's in even position; v's in odd
    }
    while (nv--) {
        cycle.insert(edge(vs[nv | 1], vs[(nv + 1) & ~1])); // u's in odd position; v's in even
    }

    for (uint32_t nonce = n = 0; nonce < ctx->m_difficulty; nonce++) {
        edge e(sipnode(&ctx->m_keys, edgeMask, nonce, 0), sipnode(&ctx->m_keys, edgeMask, nonce, 1));
        if (cycle.find(e) != cycle.end()) {
            // LogPrintf("%x ", nonce);
            cycle.erase(e);
            nonces.insert(nonce);
        }
    }
    // LogPrintf("\n");
}

bool FindCycle(const uint256& hash, uint8_t edgeBits, uint8_t proofSize, std::set<uint32_t>& cycle)
{
    assert(edgeBits >= MIN_EDGE_BITS && edgeBits <= MAX_EDGE_BITS);

    LogPrintf("Looking for %d-cycle on cuckoo%d(\"%s\") with 50% edges\n", proofSize, edgeBits + 1, hash.GetHex().c_str());

    uint32_t nodesCount = 1 << (edgeBits + 1);
    // edge mask is a max valid value of an edge.
    // edge mask is twice less then nodes count - 1
    // if nodesCount if 0x1000 then mask is 0x7ff
    uint32_t edgeMask = (1 << edgeBits) - 1;

    // set 50% difficulty - generate half of nodesCount number of edges
    uint32_t difficulty = (uint64_t)nodesCount / 2;

    auto hashStr = hash.GetHex();
    CuckooCtx ctx(hashStr.c_str(), hashStr.size(), difficulty, nodesCount);

    uint32_t timems;
    struct timeval time0, time1;

    gettimeofday(&time0, 0);

    uint32_t* cuckoo = ctx.m_cuckoo;
    uint32_t us[MAXPATHLEN], vs[MAXPATHLEN];
    for (uint32_t nonce = 0; nonce < ctx.m_difficulty; nonce++) {
        uint32_t u0 = sipnode(&ctx.m_keys, edgeMask, nonce, 0);
        if (u0 == 0) continue; // reserve 0 as nil; v0 guaranteed non-zero
        uint32_t v0 = sipnode(&ctx.m_keys, edgeMask, nonce, 1);
        uint32_t u = cuckoo[u0], v = cuckoo[v0];
        us[0] = u0;
        vs[0] = v0;

        int nu = path(cuckoo, u, us), nv = path(cuckoo, v, vs);
        if (us[nu] == vs[nv]) {
            int min = nu < nv ? nu : nv;
            for (nu -= min, nv -= min; us[nu] != vs[nv]; nu++, nv++)
                ;
            int len = nu + nv + 1;
            printf("% 4d-cycle found at %d%%\n", len, (int)(nonce * 100L / difficulty));
            if (len == proofSize) {
                solution(&ctx, us, nu, vs, nv, cycle, edgeMask);

                gettimeofday(&time1, 0);
                timems = (time1.tv_sec - time0.tv_sec) * 1000 + (time1.tv_usec - time0.tv_usec) / 1000;
                printf("Time: %d ms\n", timems);

                return true;
            }
            continue;
        }
        if (nu < nv) {
            while (nu--)
                cuckoo[us[nu + 1]] = us[nu];
            cuckoo[u0] = v0;
        } else {
            while (nv--)
                cuckoo[vs[nv + 1]] = vs[nv];
            cuckoo[v0] = u0;
        }
    }

    gettimeofday(&time1, 0);
    timems = (time1.tv_sec - time0.tv_sec) * 1000 + (time1.tv_usec - time0.tv_usec) / 1000;
    printf("Time: %d ms\n", timems);

    return false;
}

// check it easiness makes any sence here
// verify that nonces are ascending and form a cycle in header-generated graph
int VerifyCycle(const uint256& hash, uint8_t edgeBits, uint8_t proofSize, const std::vector<uint32_t>& cycle)
{
    assert(cycle.size() == proofSize);
    assert(edgeBits >= MIN_EDGE_BITS && edgeBits <= MAX_EDGE_BITS);
    siphash_keys keys;

    // edge mask is a max valid value of an edge (max index of nodes array).
    uint32_t edgeMask = (1 << edgeBits) - 1;

    auto hashStr = hash.GetHex();

    setKeys(hashStr.c_str(), hashStr.size(), &keys);

    std::vector<uint32_t> uvs(2 * proofSize);
    uint32_t xor0 = 0, xor1 = 0;

    for (uint32_t n = 0; n < proofSize; n++) {
        if (cycle[n] > edgeMask) {
            return POW_TOO_BIG;
        }

        if (n && cycle[n] <= cycle[n - 1]) {
            return POW_TOO_SMALL;
        }

        xor0 ^= uvs[2 * n] = sipnode(&keys, edgeMask, cycle[n], 0);
        xor1 ^= uvs[2 * n + 1] = sipnode(&keys, edgeMask, cycle[n], 1);
    }

    // matching endpoints imply zero xors
    if (xor0 | xor1) {
        return POW_NON_MATCHING;
    }

    uint32_t n = 0, i = 0, j;
    do { // follow cycle
        for (uint32_t k = j = i; (k = (k + 2) % (2 * proofSize)) != i;) {
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

    return n == proofSize ? POW_OK : POW_SHORT_CYCLE;
}
