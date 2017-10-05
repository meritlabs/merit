// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2016 John Tromp

#include "miner.h"
#include "cuckoo.h"
#include "hash.h"
#include "pow.h"
#include <assert.h>
#include <numeric>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

// assume EDGEBITS < 31
#define NNODES (2 * NEDGES)
#define MAXPATHLEN 8192

const char* nonceToHeader(const char* header, const uint32_t headerlen, const uint32_t nonce)
{
    ((uint32_t*)header)[headerlen / sizeof(uint32_t) - 1] = htole32(nonce); // place nonce at end

    return header;
}

class cuckoo_ctx
{
public:
    siphash_keys m_Keys;
    uint32_t m_difficulty;
    uint32_t* m_cuckoo;

    cuckoo_ctx(const char* header, const uint32_t headerlen, const uint32_t nonce, uint32_t difficulty)
    {
        SetKeys(nonceToHeader(header, headerlen, nonce), headerlen, &m_Keys);

        m_difficulty = difficulty;
        m_cuckoo = (uint32_t*)calloc(1 + NNODES, sizeof(uint32_t));

        assert(m_cuckoo != 0);
    }
    ~cuckoo_ctx()
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

void solution(cuckoo_ctx* ctx, uint32_t* us, int nu, uint32_t* vs, int nv, std::set<uint32_t>& nonces)
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
        edge e(sipnode(&ctx->m_Keys, nonce, 0), sipnode(&ctx->m_Keys, nonce, 1));
        if (cycle.find(e) != cycle.end()) {
            cycle.erase(e);
            nonces.insert(nonce);
        }
    }
}

bool worker(cuckoo_ctx* ctx, std::set<uint32_t>& cycle)
{
    uint32_t* cuckoo = ctx->m_cuckoo;
    uint32_t us[MAXPATHLEN], vs[MAXPATHLEN];
    for (uint32_t nonce = 0; nonce < ctx->m_difficulty; nonce++) {
        uint32_t u0 = sipnode(&ctx->m_Keys, nonce, 0);
        if (u0 == 0) continue; // reserve 0 as nil; v0 guaranteed non-zero
        uint32_t v0 = sipnode(&ctx->m_Keys, nonce, 1);
        uint32_t u = cuckoo[u0], v = cuckoo[v0];
        us[0] = u0;
        vs[0] = v0;

        int nu = path(cuckoo, u, us), nv = path(cuckoo, v, vs);
        if (us[nu] == vs[nv]) {
            int min = nu < nv ? nu : nv;
            for (nu -= min, nv -= min; us[nu] != vs[nv]; nu++, nv++)
                ;
            int len = nu + nv + 1;
            if (len == PROOFSIZE) {
                printf("% 4d-cycle found at %d%%\n", len, (int)(nonce * 100L / ctx->m_difficulty));
                solution(ctx, us, nu, vs, nv, cycle);
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

namespace cuckoo
{
bool FindProofOfWork(uint256 hash, int nonce, unsigned int nBits, std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.empty());

    int ratio = params.nCuckooDifficulty;

    assert(ratio >= 0 && ratio <= 100);
    u64 difficulty = ratio * (u64)NNODES / 100;

    printf("Looking for %d-cycle on cuckoo%d(\"%s\") with %d%% edges and %d nonce\n", PROOFSIZE, EDGEBITS + 1, hash.GetHex().c_str(), ratio, nonce);

    cuckoo_ctx ctx(reinterpret_cast<char*>(hash.begin()), hash.size(), nonce, difficulty);

    auto res = worker(&ctx, cycle);

    // if cycle is found check that hash of that cycle is less than a difficulty (old school bitcoin pow)
    if (res && ::CheckProofOfWork(SerializeHash(cycle), nBits, params)) {
        return true;
    }

    cycle.clear();

    return false;
}

bool VerifyProofOfWork(uint256 hash, int nonce, unsigned int nBits, const std::set<uint32_t>& cycle, const Consensus::Params& params)
{
    assert(cycle.size() == PROOFSIZE);

    siphash_keys keys;

    const char* header = reinterpret_cast<char*>(hash.begin());
    uint32_t headerlen = hash.size();

    SetKeys(nonceToHeader(header, headerlen, nonce), headerlen, &keys);

    std::vector<uint32_t> vCycle{cycle.begin(), cycle.end()};

    int res = VerifyCycle(vCycle, &keys, params.nCuckooProofSize);

    if (res == verify_code::POW_OK) {
        // check that hash of a cycle is less than a difficulty (old school bitcoin pow)
        return ::CheckProofOfWork(SerializeHash(cycle), nBits, params);
    }

    return false;
}
}
