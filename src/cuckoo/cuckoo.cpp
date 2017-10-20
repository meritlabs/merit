// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp
// Copyright (c) 2017-2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// inspired by https://github.com/tromp/cuckoo/commit/65cabf4651a8e572e99714699fbeb669565910af

#include "cuckoo.h"
#include "crypto/blake2/blake2.h"
#include "hash.h"

#include <stdint.h> // for types uint32_t,uint64_t
#include <string.h> // for functions strlen, memset

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

const char* errstr[] = {
  "OK",
  "wrong header length",
  "nonce too big",
  "nonces not ascending",
  "endpoints don't match up",
  "branch in cycle",
  "cycle dead ends",
  "cycle too short"};

// siphash uses a pair of 64-bit keys,
typedef struct {
    uint64_t k0;
    uint64_t k1;
} siphash_keys;

// generate edge endpoint in cuckoo graph
uint32_t sipnode(CSipHasher* hasher, uint32_t nonce, uint32_t uorv)
{
    auto node = CSipHasher(*hasher).Write(2 * nonce + uorv).Finalize() & EDGEMASK;

    return node << 1 | uorv;
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

class CuckooCtx
{
public:
    CSipHasher* m_hasher;
    siphash_keys m_keys;
    uint32_t m_difficulty;
    uint32_t* m_cuckoo;

    CuckooCtx(char* header, const uint32_t headerlen, uint32_t difficulty)
    {
        setKeys(header, headerlen, &m_keys);
        m_hasher = new CSipHasher(m_keys.k0, m_keys.k1);

        m_difficulty = difficulty;
        m_cuckoo = (uint32_t*)calloc(1 + NNODES, sizeof(uint32_t));

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
            printf("nu is %d\n", nu);
            while (nu-- && us[nu] != u)
                ;
            if (nu < 0)
                printf("maximum path length exceeded\n");
            else
                printf("illegal % 4d-cycle\n", MAXPATHLEN - nu);
            exit(0);
        }
        us[nu] = u;
    }
    return nu;
}

typedef std::pair<uint32_t, uint32_t> edge;

void solution(CuckooCtx* ctx, uint32_t* us, int nu, uint32_t* vs, int nv, std::set<uint32_t>& nonces)
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
        edge e(sipnode(ctx->m_hasher, nonce, 0), sipnode(ctx->m_hasher, nonce, 1));
        if (cycle.find(e) != cycle.end()) {
            // printf("%x ", nonce);
            cycle.erase(e);
            nonces.insert(nonce);
        }
    }

    printf("\n");
}

bool FindCycle(const uint256& hash, std::set<uint32_t>& cycle, uint8_t proofsize, uint8_t ratio)
{
    assert(ratio >= 0 && ratio <= 100);
    uint64_t difficulty = ratio * (uint64_t)NNODES / 100;

    printf("Looking for %d-cycle on cuckoo%d(\"%s\") with %d%% edges\n", proofsize, EDGEBITS + 1, hash.GetHex().c_str(), ratio);

    CuckooCtx ctx(const_cast<char*>(reinterpret_cast<const char*>(hash.begin())), hash.size(), difficulty);

    uint32_t* cuckoo = ctx.m_cuckoo;
    uint32_t us[MAXPATHLEN], vs[MAXPATHLEN];
    for (uint32_t nonce = 0; nonce < ctx.m_difficulty; nonce++) {
        uint32_t u0 = sipnode(ctx.m_hasher, nonce, 0);
        if (u0 == 0) continue; // reserve 0 as nil; v0 guaranteed non-zero
        uint32_t v0 = sipnode(ctx.m_hasher, nonce, 1);
        uint32_t u = cuckoo[u0], v = cuckoo[v0];
        us[0] = u0;
        vs[0] = v0;

        int nu = path(cuckoo, u, us), nv = path(cuckoo, v, vs);
        if (us[nu] == vs[nv]) {
            int min = nu < nv ? nu : nv;
            for (nu -= min, nv -= min; us[nu] != vs[nv]; nu++, nv++)
                ;
            int len = nu + nv + 1;
            if (len == proofsize) {
                printf("% 4d-cycle found at %d%%\n", len, (int)(nonce * 100L / ctx.m_difficulty));
                solution(&ctx, us, nu, vs, nv, cycle);
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

    return false;
}

// verify that nonces are ascending and form a cycle in header-generated graph
int VerifyCycle(const uint256& hash, std::vector<uint32_t>& cycle, const uint8_t proofsize)
{
    assert(cycle.size() == proofsize);

    siphash_keys keys;

    char* header = const_cast<char*>(reinterpret_cast<const char*>(hash.begin()));
    uint32_t headerlen = hash.size();

    setKeys(header, headerlen, &keys);

    CSipHasher hasher{keys.k0, keys.k1};

    uint32_t uvs[2 * proofsize];
    uint32_t xor0 = 0, xor1 = 0;

    for (uint32_t n = 0; n < proofsize; n++) {
        if (cycle[n] > EDGEMASK) {
            return POW_TOO_BIG;
        }

        if (n && cycle[n] <= cycle[n - 1]) {
            return POW_TOO_SMALL;
        }

        xor0 ^= uvs[2 * n] = sipnode(&hasher, cycle[n], 0);
        xor1 ^= uvs[2 * n + 1] = sipnode(&hasher, cycle[n], 1);
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
