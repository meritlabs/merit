// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2018 John Tromp

#include "mean_cuckoo.h"
#include "cuckoo.h"

#ifdef __APPLE__
#include "osx_barrier.h"
#endif

#include <bitset>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <x86intrin.h>

// algorithm/performance parameters

// EDGEBITS/NEDGES/EDGEMASK defined in cuckoo.h

// The node bits are logically split into 3 groups:
// XBITS 'X' bits (most significant), YBITS 'Y' bits, and ZBITS 'Z' bits (least significant)
// Here we have the default XBITS=YBITS=7, ZBITS=15 summing to EDGEBITS=29
// nodebits   XXXXXXX YYYYYYY ZZZZZZZZZZZZZZZ
// bit%10     8765432 1098765 432109876543210
// bit/10     2222222 2111111 111110000000000

// The matrix solver stores all edges in a matrix of NX * NX buckets,
// where NX = 2^XBITS is the number of possible values of the 'X' bits.
// Edge i between nodes ui = siphash24(2*i) and vi = siphash24(2*i+1)
// resides in the bucket at (uiX,viX)
// In each trimming round, either a matrix row or a matrix column (NX buckets)
// is bucket sorted on uY or vY respectively, and then within each bucket
// uZ or vZ values are counted and edges with a count of only one are eliminated,
// while remaining edges are bucket sorted back on vX or uX respectively.
// When sufficiently many edges have been eliminated, a pair of compression
// rounds remap surviving Y,Z values in each row or column into 15 bit
// combined YZ values, allowing the remaining rounds to avoid the sorting on Y,
// and directly count YZ values in a cache friendly 32KB.
// A final pair of compression rounds remap YZ values from 15 into 11 bits.


#ifndef NSIPHASH
#define NSIPHASH 1
#endif


#ifndef XBITS
// 7 seems to give best performance
#define XBITS 7
#endif

#define YBITS XBITS
#define PROOFSIZE 42

// proof-of-work parameters
#ifndef EDGEBITS
// the main parameter is the 2-log of the graph size,
// which is the size in bits of the node identifiers
#define EDGEBITS 27
#endif


// size in bytes of a big bucket entry
#ifndef BIGSIZE
#if EDGEBITS <= 15
#define BIGSIZE 4
// no compression needed
#define COMPRESSROUND 0
#else
#define BIGSIZE 5
// YZ compression round; must be even
#ifndef COMPRESSROUND
#define COMPRESSROUND 14
#endif
#endif
#endif
// size in bytes of a small bucket entry
#define SMALLSIZE BIGSIZE

// initial entries could be smaller at percent or two slowdown
#ifndef BIGSIZE0
#if EDGEBITS < 30 && !defined SAVEEDGES
#define BIGSIZE0 4
#else
#define BIGSIZE0 BIGSIZE
#endif
#endif
// but they may need syncing entries
#if BIGSIZE0 == 4 && EDGEBITS > 27
#define NEEDSYNC
#endif

typedef uint8_t uint8_t;
typedef uint16_t uint16_t;

#if EDGEBITS >= 30
typedef uint64_t offset_t;
#else
typedef uint32_t offset_t;
#endif

#if BIGSIZE0 > 4
typedef uint64_t BIGTYPE0;
#else
typedef uint32_t BIGTYPE0;
#endif

// number of edges
#define NEDGES ((uint32_t)1 << EDGEBITS)
// used to mask siphash output
#define EDGEMASK ((uint32_t)NEDGES - 1)

// typedef uint32_t BIGTYPE0;

// typedef uint32_t offset_t;

typedef uint32_t proof[PROOFSIZE];

// node bits have two groups of bucketbits (X for big and Y for small) and a remaining group Z of degree bits
const static uint32_t NX = 1 << XBITS;
const static uint32_t XMASK = NX - 1;
const static uint32_t NY = 1 << YBITS;
const static uint32_t YMASK = NY - 1;
const static uint32_t XYBITS = XBITS + YBITS;
const static uint32_t NXY = 1 << XYBITS;
const static uint32_t ZBITS = EDGEBITS - XYBITS;
const static uint32_t NZ = 1 << ZBITS;
const static uint32_t ZMASK = NZ - 1;
const static uint32_t YZBITS = EDGEBITS - XBITS;
const static uint32_t NYZ = 1 << YZBITS;
const static uint32_t YZMASK = NYZ - 1;
const static uint32_t YZ1BITS = YZBITS < 15 ? YZBITS : 15; // compressed YZ bits
const static uint32_t NYZ1 = 1 << YZ1BITS;
const static uint32_t YZ1MASK = NYZ1 - 1;
const static uint32_t Z1BITS = YZ1BITS - YBITS;
const static uint32_t NZ1 = 1 << Z1BITS;
const static uint32_t Z1MASK = NZ1 - 1;
const static uint32_t YZ2BITS = YZBITS < 11 ? YZBITS : 11; // more compressed YZ bits
const static uint32_t NYZ2 = 1 << YZ2BITS;
const static uint32_t YZ2MASK = NYZ2 - 1;
const static uint32_t Z2BITS = YZ2BITS - YBITS;
const static uint32_t NZ2 = 1 << Z2BITS;
const static uint32_t Z2MASK = NZ2 - 1;
const static uint32_t YZZBITS = YZBITS + ZBITS;
const static uint32_t YZZ1BITS = YZ1BITS + ZBITS;

const static uint32_t BIGSLOTBITS = BIGSIZE * 8;
const static uint32_t SMALLSLOTBITS = SMALLSIZE * 8;
const static uint64_t BIGSLOTMASK = (1ULL << BIGSLOTBITS) - 1ULL;
const static uint64_t SMALLSLOTMASK = (1ULL << SMALLSLOTBITS) - 1ULL;
const static uint32_t BIGSLOTBITS0 = BIGSIZE0 * 8;
const static uint64_t BIGSLOTMASK0 = (1ULL << BIGSLOTBITS0) - 1ULL;
const static uint32_t NONYZBITS = BIGSLOTBITS0 - YZBITS;
const static uint32_t NNONYZ = 1 << NONYZBITS;


// grow with cube root of size, hardly affected by trimming
const static uint32_t CUCKOO_SIZE = 2 * NX * NYZ2;

// for p close to 0, Pr(X>=k) < e^{-n*p*eps^2} where k=n*p*(1+eps)
// see https://en.wikipedia.org/wiki/Binomial_distribution#Tail_bounds
// eps should be at least 1/sqrt(n*p/64)
// to give negligible bad odds of e^-64.

// 1/32 reduces odds of overflowing z bucket on 2^30 nodes to 2^14*e^-32
// (less than 1 in a billion) in theory. not so in practice (fails first at mean30 -n 1549)
#ifndef BIGEPS
#define BIGEPS 5 / 64
#endif

// 176/256 is safely over 1-e(-1) ~ 0.63 trimming fraction
#ifndef TRIMFRAC256
#define TRIMFRAC256 184
#endif

const static uint32_t NTRIMMEDZ = NZ * TRIMFRAC256 / 256;

const static uint32_t ZBUCKETSLOTS = NZ + NZ * BIGEPS;
const static uint32_t ZBUCKETSIZE = ZBUCKETSLOTS * BIGSIZE0;
const static uint32_t TBUCKETSIZE = ZBUCKETSLOTS * BIGSIZE;

template <uint32_t BUCKETSIZE>
struct zbucket {
    uint32_t size;
    const static uint32_t RENAMESIZE = 2 * NZ2 + 2 * (COMPRESSROUND ? NZ1 : 0);
    union alignas(16) {
        uint8_t bytes[BUCKETSIZE];
        struct {
            uint32_t words[BUCKETSIZE / sizeof(uint32_t) - RENAMESIZE];
            uint32_t renameu1[NZ2];
            uint32_t renamev1[NZ2];
            uint32_t renameu[COMPRESSROUND ? NZ1 : 0];
            uint32_t renamev[COMPRESSROUND ? NZ1 : 0];
        };
    };
    uint32_t setsize(uint8_t const* end)
    {
        size = end - bytes; // bytes is an address of the begining of bytes array, end is the address of it's end
        // printf("size: %d, BUCKETSIZE: %d\n", size, BUCKETSIZE);
        assert(size <= BUCKETSIZE);
        return size;
    }
};

template <uint32_t BUCKETSIZE>
using yzbucket = zbucket<BUCKETSIZE>[NY];
template <uint32_t BUCKETSIZE>
using matrix = yzbucket<BUCKETSIZE>[NX];

template <uint32_t BUCKETSIZE>
struct indexer {
    offset_t index[NX]; // uint32_t[128] - array of addresses in trimmer->buckets matrix row or column

    void matrixv(const uint32_t y)
    {
        const yzbucket<BUCKETSIZE>* foo = 0;
        for (uint32_t x = 0; x < NX; x++)
            index[x] = foo[x][y].bytes - (uint8_t*)foo;
    }
    offset_t storev(yzbucket<BUCKETSIZE>* buckets, const uint32_t y)
    {
        uint8_t const* base = (uint8_t*)buckets;
        offset_t sumsize = 0;
        for (uint32_t x = 0; x < NX; x++) {
            // printf("base: %8x; x: %3d, index[x]: %8x\n", base, x, index[x]);
            sumsize += buckets[x][y].setsize(base + index[x]);
        }
        return sumsize;
    }
    void matrixu(const uint32_t x)
    {
        const yzbucket<BUCKETSIZE>* foo = 0;
        for (uint32_t y = 0; y < NY; y++)
            index[y] = foo[x][y].bytes - (uint8_t*)foo;
    }
    offset_t storeu(yzbucket<BUCKETSIZE>* buckets, const uint32_t x)
    {
        uint8_t const* base = (uint8_t*)buckets;
        offset_t sumsize = 0;
        for (uint32_t y = 0; y < NY; y++)
            sumsize += buckets[x][y].setsize(base + index[y]);
        return sumsize;
    }
};

#define likely(x) __builtin_expect((x) != 0, 1)
#define unlikely(x) __builtin_expect((x), 0)

class edgetrimmer; // avoid circular references

typedef struct {
    uint32_t id;
    pthread_t thread;
    edgetrimmer* et;
} thread_ctx;

typedef uint8_t zbucket8[2 * NYZ1];
typedef uint16_t zbucket16[NTRIMMEDZ];
typedef uint32_t zbucket32[NTRIMMEDZ];

// maintains set of trimmable edges
class edgetrimmer
{
public:
    siphash_keys sip_keys;
    yzbucket<ZBUCKETSIZE>* buckets;
    yzbucket<TBUCKETSIZE>* tbuckets;
    zbucket32* tedges;
    zbucket16* tzs;
    zbucket8* tdegs;
    offset_t* tcounts;
    uint32_t ntrims;
    uint32_t nthreads;
    bool showall;
    pthread_barrier_t barry;
    uint32_t edgeMask;
    CSipHasher* hasher;

    void touch(uint8_t* p, const offset_t n)
    {
        for (offset_t i = 0; i < n; i += 4096)
            *(uint32_t*)(p + i) = 0;
    }
    edgetrimmer(const uint32_t n_threads, const uint32_t n_trims, uint32_t edgeMaskIn, const bool show_all)
    {
        assert(sizeof(matrix<ZBUCKETSIZE>) == NX * sizeof(yzbucket<ZBUCKETSIZE>));
        assert(sizeof(matrix<TBUCKETSIZE>) == NX * sizeof(yzbucket<TBUCKETSIZE>));
        nthreads = n_threads;
        ntrims = n_trims;
        showall = show_all;
        buckets = new yzbucket<ZBUCKETSIZE>[NX];
        touch((uint8_t*)buckets, sizeof(matrix<ZBUCKETSIZE>));
        tbuckets = new yzbucket<TBUCKETSIZE>[nthreads];
        touch((uint8_t*)tbuckets, nthreads * sizeof(yzbucket<TBUCKETSIZE>));

        tedges = new zbucket32[nthreads];
        edgeMask = edgeMaskIn;

        tdegs = new zbucket8[nthreads];
        tzs = new zbucket16[nthreads];
        tcounts = new offset_t[nthreads];
        int err = pthread_barrier_init(&barry, NULL, nthreads);
        assert(err == 0);
    }
    ~edgetrimmer()
    {
        delete[] buckets;
        delete[] tbuckets;
        delete[] tedges;
        delete[] tdegs;
        delete[] tzs;
        delete[] tcounts;
    }
    offset_t count() const
    {
        offset_t cnt = 0;
        for (uint32_t t = 0; t < nthreads; t++)
            cnt += tcounts[t];
        return cnt;
    }

    void InitHasher()
    {
        hasher = new CSipHasher(sip_keys.k0, sip_keys.k1);
    }

    void genUnodes(const uint32_t id, const uint32_t uorv)
    {
        uint64_t rdtsc0, rdtsc1;
        rdtsc0 = __rdtsc();

        uint8_t const* base = (uint8_t*)buckets;
        indexer<ZBUCKETSIZE> dst;
        const uint32_t starty = NY * id / nthreads;     // 0 for nthreads = 1
        const uint32_t endy = NY * (id + 1) / nthreads; // 128 for nthreads = 1
        uint32_t edge = starty << YZBITS;               // 0 as starty is 0
        uint32_t endedge = edge + NYZ;                  // 0 + 2^(7 + 13) = 1 048 576

        // printf("starty: %d; endy: %d\n", starty, endy);
        // printf("edge: %d; endedge: %d\n", edge, endedge);

        offset_t sumsize = 0;
        for (uint32_t my = starty; my < endy; my++, endedge += NYZ) {
            dst.matrixv(my);

            // printf("my: %d; endedge: %d\n", my, endedge);

            // edge is a "nonce" for sipnode()
            for (; edge < endedge; edge += NSIPHASH) {
// bit        28..21     20..13    12..0
// node       XXXXXX     YYYYYY    ZZZZZ
#if NSIPHASH == 1
                const uint32_t node = _sipnode(&sip_keys, edgeMask, edge, uorv); // node - generated random node for the graph

                const uint32_t ux = node >> YZBITS;                             // ux - highest X (7) bits
                const BIGTYPE0 zz = (BIGTYPE0)edge << YZBITS | (node & YZMASK); // - edge YYYYYY ZZZZZ

                if (edge % 100000 == 0) {
                    // printf("edge: %x, node: %7x; ux: %2x, zz: %8x, dst.index[ux]: %8x\n", edge, node, ux, zz, dst.index[ux]);
                }
                // bit        39..21     20..13    12..0
                // write        edge     YYYYYY    ZZZZZ
                *(BIGTYPE0*)(base + dst.index[ux]) = zz;
                dst.index[ux] += BIGSIZE0;

#else
#error not implemented
#endif
            }
            sumsize += dst.storev(buckets, my);
        }
        rdtsc1 = __rdtsc();
        if (!id) printf("genUnodes round %2d size %u rdtsc: %lu\n", uorv, sumsize / BIGSIZE0, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / BIGSIZE0;
    }

    void genVnodes(const uint32_t id, const uint32_t uorv)
    {
        uint64_t rdtsc0, rdtsc1;

        static const uint32_t NONDEGBITS = std::min(BIGSLOTBITS, 2 * YZBITS) - ZBITS;
        static const uint32_t NONDEGMASK = (1 << NONDEGBITS) - 1;
        indexer<ZBUCKETSIZE> dst;
        indexer<TBUCKETSIZE> small;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startux = NX * id / nthreads;
        const uint32_t endux = NX * (id + 1) / nthreads;
        for (uint32_t ux = startux; ux < endux; ux++) { // matrix x == ux
            small.matrixu(0);
            for (uint32_t my = 0; my < NY; my++) {
                uint32_t edge = my << YZBITS;
                uint8_t* readbig = buckets[ux][my].bytes;
                uint8_t const* endreadbig = readbig + buckets[ux][my].size;
                // printf("id %d x %d y %d size %u read %d\n", id, ux, my, buckets[ux][my].size, readbig-base);
                for (; readbig < endreadbig; readbig += BIGSIZE0) {
                    // bit     39/31..21     20..13    12..0
                    // read         edge     UYYYYY    UZZZZ   within UX partition
                    BIGTYPE0 e = *(BIGTYPE0*)readbig;
                    edge += ((uint32_t)(e >> YZBITS) - edge) & (NNONYZ - 1);
                    // if (ux==78 && my==243) printf("id %d ux %d my %d e %08x prefedge %x edge %x\n", id, ux, my, e, e >> YZBITS, edge);
                    const uint32_t uy = (e >> ZBITS) & YMASK;
                    // bit         39..13     12..0
                    // write         edge     UZZZZ   within UX UY partition
                    *(uint64_t*)(small0 + small.index[uy]) = ((uint64_t)edge << ZBITS) | (e & ZMASK);
                    // printf("id %d ux %d y %d e %010lx e' %010x\n", id, ux, my, e, ((uint64_t)edge << ZBITS) | (e >> YBITS));
                    small.index[uy] += SMALLSIZE;
                }
                if (unlikely(edge >> NONYZBITS != (((my + 1) << YZBITS) - 1) >> NONYZBITS)) {
                    printf("OOPS1: id %d ux %d y %d edge %x vs %x\n", id, ux, my, edge, ((my + 1) << YZBITS) - 1);
                    exit(0);
                }
            }
            uint8_t* degs = tdegs[id];
            small.storeu(tbuckets + id, 0);
            dst.matrixu(ux);
            for (uint32_t uy = 0; uy < NY; uy++) {
                memset(degs, 0xff, NZ);
                uint8_t *readsmall = tbuckets[id][uy].bytes, *endreadsmall = readsmall + tbuckets[id][uy].size;
                // if (id==1) printf("id %d ux %d y %d size %u sumsize %u\n", id, ux, uy, tbuckets[id][uy].size/BIGSIZE, sumsize);
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SMALLSIZE)
                    degs[*(uint32_t*)rdsmall & ZMASK]++;
                uint16_t* zs = tzs[id];
                uint32_t* edges0 = tedges[id];
                uint32_t *edges = edges0, edge = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SMALLSIZE) {
                    // bit         39..13     12..0
                    // read          edge     UZZZZ    sorted by UY within UX partition
                    const uint64_t e = *(uint64_t*)rdsmall;
                    edge += ((e >> ZBITS) - edge) & NONDEGMASK;
                    // if (id==0) printf("id %d ux %d uy %d e %010lx pref %4x edge %x mask %x\n", id, ux, uy, e, e>>ZBITS, edge, NONDEGMASK);
                    *edges = edge;
                    const uint32_t z = e & ZMASK;
                    *zs = z;
                    const uint32_t delta = degs[z] ? 1 : 0;
                    edges += delta;
                    zs += delta;
                }
                if (unlikely(edge >> NONDEGBITS != EDGEMASK >> NONDEGBITS)) {
                    printf("OOPS2: id %d ux %d uy %d edge %x vs %x\n", id, ux, uy, edge, EDGEMASK);
                    exit(0);
                }
                assert(edges - edges0 < NTRIMMEDZ);
                const uint16_t* readz = tzs[id];
                const uint32_t* readedge = edges0;
                int64_t uy34 = (int64_t)uy << YZZBITS;

                for (; readedge < edges; readedge++, readz++) { // process up to 7 leftover edges if NSIPHASH==8
                    const uint32_t node = _sipnode(&sip_keys, edgeMask, *readedge, uorv);
                    const uint32_t vx = node >> YZBITS; // & XMASK;
                                                        // bit        39..34    33..21     20..13     12..0
                                                        // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition
                    *(uint64_t*)(base + dst.index[vx]) = uy34 | ((uint64_t)*readz << YZBITS) | (node & YZMASK);
                    // printf("id %d ux %d y %d edge %08x e' %010lx vx %d\n", id, ux, uy, *readedge, uy34 | ((uint64_t)(node & YZMASK) << ZBITS) | *readz, vx);
                    dst.index[vx] += BIGSIZE;
                }
            }
            // printf("sumsize: %d\n", sumsize);
            sumsize += dst.storeu(buckets, ux);
        }
        rdtsc1 = __rdtsc();
        if (!id) printf("genVnodes round %2d size %u rdtsc: %lu\n", uorv, sumsize / BIGSIZE, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / BIGSIZE;
    }

    template <uint32_t SRCSIZE, uint32_t DSTSIZE, bool TRIMONV>
    void trimedges(const uint32_t id, const uint32_t round)
    {
        const uint32_t SRCSLOTBITS = std::min(SRCSIZE * 8, 2 * YZBITS);
        const uint64_t SRCSLOTMASK = (1ULL << SRCSLOTBITS) - 1ULL;
        const uint32_t SRCPREFBITS = SRCSLOTBITS - YZBITS;
        const uint32_t SRCPREFMASK = (1 << SRCPREFBITS) - 1;
        const uint32_t DSTSLOTBITS = std::min(DSTSIZE * 8, 2 * YZBITS);
        const uint64_t DSTSLOTMASK = (1ULL << DSTSLOTBITS) - 1ULL;
        const uint32_t DSTPREFBITS = DSTSLOTBITS - YZZBITS;
        const uint32_t DSTPREFMASK = (1 << DSTPREFBITS) - 1;
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE> dst;
        indexer<TBUCKETSIZE> small;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startvx = NY * id / nthreads;
        const uint32_t endvx = NY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            small.matrixu(0);
            for (uint32_t ux = 0; ux < NX; ux++) {
                uint32_t uxyz = ux << YZBITS;
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                const uint8_t *readbig = zb.bytes, *endreadbig = readbig + zb.size;
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig += SRCSIZE) {
                    // bit        39..34    33..21     20..13     12..0
                    // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition
                    const uint64_t e = *(uint64_t*)readbig & SRCSLOTMASK;
                    uxyz += ((uint32_t)(e >> YZBITS) - uxyz) & SRCPREFMASK;
                    // if (round==6) printf("id %d vx %d ux %d e %010lx suffUXYZ %05x suffUXY %03x UXYZ %08x UXY %04x mask %x\n", id, vx, ux, e, (uint32_t)(e >> YZBITS), (uint32_t)(e >> YZZBITS), uxyz, uxyz>>ZBITS, SRCPREFMASK);
                    const uint32_t vy = (e >> ZBITS) & YMASK;
                    // bit     41/39..34    33..26     25..13     12..0
                    // write      UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    *(uint64_t*)(small0 + small.index[vy]) = ((uint64_t)uxyz << ZBITS) | (e & ZMASK);
                    uxyz &= ~ZMASK;
                    small.index[vy] += DSTSIZE;
                }
                if (unlikely(uxyz >> YZBITS != ux)) {
                    printf("OOPS3: id %d vx %d ux %d UXY %x\n", id, vx, ux, uxyz);
                    exit(0);
                }
            }
            uint8_t* degs = tdegs[id];
            small.storeu(tbuckets + id, 0);
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            for (uint32_t vy = 0; vy < NY; vy++) {
                const uint64_t vy34 = (uint64_t)vy << YZZBITS;
                memset(degs, 0xff, NZ);
                uint8_t *readsmall = tbuckets[id][vy].bytes, *endreadsmall = readsmall + tbuckets[id][vy].size;
                // printf("id %d vx %d vy %d size %u sumsize %u\n", id, vx, vy, tbuckets[id][vx].size/BIGSIZE, sumsize);
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += DSTSIZE)
                    degs[*(uint32_t*)rdsmall & ZMASK]++;
                uint32_t ux = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += DSTSIZE) {
                    // bit     41/39..34    33..26     25..13     12..0
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    // bit        39..37    36..30     29..15     14..0      with XBITS==YBITS==7
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    const uint64_t e = *(uint64_t*)rdsmall & DSTSLOTMASK;
                    ux += ((uint32_t)(e >> YZZBITS) - ux) & DSTPREFMASK;
                    // printf("id %d vx %d vy %d e %010lx suffUX %02x UX %x mask %x\n", id, vx, vy, e, (uint32_t)(e >> YZZBITS), ux, SRCPREFMASK);
                    // bit    41/39..34    33..21     20..13     12..0
                    // write     VYYYYY    VZZZZZ     UYYYYY     UZZZZ   within UX partition
                    *(uint64_t*)(base + dst.index[ux]) = vy34 | ((e & ZMASK) << YZBITS) | ((e >> ZBITS) & YZMASK);
                    dst.index[ux] += degs[e & ZMASK] ? DSTSIZE : 0;
                }
                if (unlikely(ux >> DSTPREFBITS != XMASK >> DSTPREFBITS)) {
                    printf("OOPS4: id %d vx %x ux %x vs %x\n", id, vx, ux, XMASK);
                }
            }
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        if (showall || (!id && !(round & (round + 1))))
            printf("trimedges round %2d size %u rdtsc: %lu\n", round, sumsize / DSTSIZE, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / DSTSIZE;
    }

    template <uint32_t SRCSIZE, uint32_t DSTSIZE, bool TRIMONV>
    void trimrename(const uint32_t id, const uint32_t round)
    {
        const uint32_t SRCSLOTBITS = std::min(SRCSIZE * 8, (TRIMONV ? YZBITS : YZ1BITS) + YZBITS);
        const uint64_t SRCSLOTMASK = (1ULL << SRCSLOTBITS) - 1ULL;
        const uint32_t SRCPREFBITS = SRCSLOTBITS - YZBITS;
        const uint32_t SRCPREFMASK = (1 << SRCPREFBITS) - 1;
        const uint32_t SRCPREFBITS2 = SRCSLOTBITS - YZZBITS;
        const uint32_t SRCPREFMASK2 = (1 << SRCPREFBITS2) - 1;
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE> dst;
        indexer<TBUCKETSIZE> small;
        static uint32_t maxnnid = 0;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startvx = NY * id / nthreads;
        const uint32_t endvx = NY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            small.matrixu(0);
            for (uint32_t ux = 0; ux < NX; ux++) {
                uint32_t uyz = 0;
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                const uint8_t *readbig = zb.bytes, *endreadbig = readbig + zb.size;
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig += SRCSIZE) {
                    // bit        39..37    36..22     21..15     14..0
                    // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition  if TRIMONV
                    // bit            36...22     21..15     14..0
                    // write          VYYYZZ'     UYYYYY     UZZZZ   within UX partition  if !TRIMONV
                    const uint64_t e = *(uint64_t*)readbig & SRCSLOTMASK;
                    if (TRIMONV)
                        uyz += ((uint32_t)(e >> YZBITS) - uyz) & SRCPREFMASK;
                    else
                        uyz = e >> YZBITS;
                    // if (round==32 && ux==25) printf("id %d vx %d ux %d e %010lx suffUXYZ %05x suffUXY %03x UXYZ %08x UXY %04x mask %x\n", id, vx, ux, e, (uint32_t)(e >> YZBITS), (uint32_t)(e >> YZZBITS), uxyz, uxyz>>ZBITS, SRCPREFMASK);
                    const uint32_t vy = (e >> ZBITS) & YMASK;
                    // bit        39..37    36..30     29..15     14..0
                    // write      UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition  if TRIMONV
                    // bit            36...30     29...15     14..0
                    // write          VXXXXXX     VYYYZZ'     UZZZZ   within UX UY partition  if !TRIMONV
                    *(uint64_t*)(small0 + small.index[vy]) = ((uint64_t)(ux << (TRIMONV ? YZBITS : YZ1BITS) | uyz) << ZBITS) | (e & ZMASK);
                    // if (TRIMONV&&vx==75&&vy==83) printf("id %d vx %d vy %d e %010lx e15 %x ux %x\n", id, vx, vy, ((uint64_t)uxyz << ZBITS) | (e & ZMASK), uxyz, uxyz>>YZBITS);
                    if (TRIMONV)
                        uyz &= ~ZMASK;
                    small.index[vy] += SRCSIZE;
                }
            }
            uint16_t* degs = (uint16_t*)tdegs[id];
            small.storeu(tbuckets + id, 0);
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            uint32_t newnodeid = 0;
            uint32_t* renames = TRIMONV ? buckets[0][vx].renamev : buckets[vx][0].renameu;
            uint32_t* endrenames = renames + NZ1;
            for (uint32_t vy = 0; vy < NY; vy++) {
                memset(degs, 0xff, 2 * NZ);
                uint8_t *readsmall = tbuckets[id][vy].bytes, *endreadsmall = readsmall + tbuckets[id][vy].size;
                // printf("id %d vx %d vy %d size %u sumsize %u\n", id, vx, vy, tbuckets[id][vx].size/BIGSIZE, sumsize);
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE)
                    degs[*(uint32_t*)rdsmall & ZMASK]++;
                uint32_t ux = 0;
                uint32_t nrenames = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE) {
                    // bit        39..37    36..30     29..15     14..0
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition  if TRIMONV
                    // bit            36...30     29...15     14..0
                    // read           VXXXXXX     VYYYZZ'     UZZZZ   within UX UY partition  if !TRIMONV
                    const uint64_t e = *(uint64_t*)rdsmall & SRCSLOTMASK;
                    if (TRIMONV)
                        ux += ((uint32_t)(e >> YZZBITS) - ux) & SRCPREFMASK2;
                    else
                        ux = e >> YZZ1BITS;
                    const uint32_t vz = e & ZMASK;
                    uint16_t vdeg = degs[vz];
                    // if (TRIMONV&&vx==75&&vy==83) printf("id %d vx %d vy %d e %010lx e37 %x ux %x vdeg %d nrenames %d\n", id, vx, vy, e, e>>YZZBITS, ux, vdeg, nrenames);
                    if (vdeg) {
                        if (vdeg < 32) {
                            degs[vz] = vdeg = 32 + nrenames++;
                            *renames++ = vy << ZBITS | vz;
                            if (renames == endrenames) {
                                endrenames += (TRIMONV ? sizeof(yzbucket<ZBUCKETSIZE>) : sizeof(zbucket<ZBUCKETSIZE>)) / sizeof(uint32_t);
                                renames = endrenames - NZ1;
                            }
                        }
                        // bit       36..22     21..15     14..0
                        // write     VYYZZ'     UYYYYY     UZZZZ   within UX partition  if TRIMONV
                        if (TRIMONV)
                            *(uint64_t*)(base + dst.index[ux]) = ((uint64_t)(newnodeid + vdeg - 32) << YZBITS) | ((e >> ZBITS) & YZMASK);
                        else
                            *(uint32_t*)(base + dst.index[ux]) = ((newnodeid + vdeg - 32) << YZ1BITS) | ((e >> ZBITS) & YZ1MASK);
                        // if (vx==44&&vy==58) printf("  id %d vx %d vy %d newe %010lx\n", id, vx, vy, vy28 | ((vdeg) << YZBITS) | ((e >> ZBITS) & YZMASK));
                        dst.index[ux] += DSTSIZE;
                    }
                }
                newnodeid += nrenames;
                if (TRIMONV && unlikely(ux >> SRCPREFBITS2 != XMASK >> SRCPREFBITS2)) {
                    printf("OOPS6: id %d vx %d vy %d ux %x vs %x\n", id, vx, vy, ux, XMASK);
                    exit(0);
                }
            }
            if (newnodeid > maxnnid)
                maxnnid = newnodeid;
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        if (showall || !id) printf("trimrename round %2d size %u rdtsc: %lu maxnnid %d\n", round, sumsize / DSTSIZE, rdtsc1 - rdtsc0, maxnnid);
        assert(maxnnid < NYZ1);
        tcounts[id] = sumsize / DSTSIZE;
    }

    template <bool TRIMONV>
    void trimedges1(const uint32_t id, const uint32_t round)
    {
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE> dst;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t* degs = tdegs[id];
        uint8_t const* base = (uint8_t*)buckets;
        const uint32_t startvx = NY * id / nthreads;
        const uint32_t endvx = NY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            memset(degs, 0xff, NYZ1);
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %d\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig++)
                    degs[*readbig & YZ1MASK]++;
            }
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                for (; readbig < endreadbig; readbig++) {
                    // bit       29..22    21..15     14..7     6..0
                    // read      UYYYYY    UZZZZ'     VYYYY     VZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t vyz = e & YZ1MASK;
                    // printf("id %d vx %d ux %d e %08lx vyz %04x uyz %04x\n", id, vx, ux, e, vyz, e >> YZ1BITS);
                    // bit       29..22    21..15     14..7     6..0
                    // write     VYYYYY    VZZZZ'     UYYYY     UZZ'   within UX partition
                    *(uint32_t*)(base + dst.index[ux]) = (vyz << YZ1BITS) | (e >> YZ1BITS);
                    dst.index[ux] += degs[vyz] ? sizeof(uint32_t) : 0;
                }
            }
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        if (showall || (!id && !(round & (round + 1))))
            printf("trimedges1 round %2d size %u rdtsc: %lu\n", round, sumsize / sizeof(uint32_t), rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / sizeof(uint32_t);
    }

    template <bool TRIMONV>
    void trimrename1(const uint32_t id, const uint32_t round)
    {
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE> dst;
        static uint32_t maxnnid = 0;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint16_t* degs = (uint16_t*)tdegs[id];
        uint8_t const* base = (uint8_t*)buckets;
        const uint32_t startvx = NY * id / nthreads;
        const uint32_t endvx = NY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            memset(degs, 0xff, 2 * NYZ1); // sets each uint16_t entry to 0xffff
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %d\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig++)
                    degs[*readbig & YZ1MASK]++;
            }
            uint32_t newnodeid = 0;
            uint32_t* renames = TRIMONV ? buckets[0][vx].renamev1 : buckets[vx][0].renameu1;
            uint32_t* endrenames = renames + NZ2;
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                for (; readbig < endreadbig; readbig++) {
                    // bit       29...15     14...0
                    // read      UYYYZZ'     VYYZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t vyz = e & YZ1MASK;
                    uint16_t vdeg = degs[vyz];
                    if (vdeg) {
                        if (vdeg < 32) {
                            degs[vyz] = vdeg = 32 + newnodeid++;
                            *renames++ = vyz;
                            if (renames == endrenames) {
                                endrenames += (TRIMONV ? sizeof(yzbucket<ZBUCKETSIZE>) : sizeof(zbucket<ZBUCKETSIZE>)) / sizeof(uint32_t);
                                renames = endrenames - NZ2;
                            }
                        }
                        // bit       25...15     14...0
                        // write     VYYZZZ"     UYYZZ'   within UX partition
                        *(uint32_t*)(base + dst.index[ux]) = ((vdeg - 32) << (TRIMONV ? YZ1BITS : YZ2BITS)) | (e >> YZ1BITS);
                        dst.index[ux] += sizeof(uint32_t);
                    }
                }
            }
            if (newnodeid > maxnnid)
                maxnnid = newnodeid;
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        if (showall || !id) printf("trimrename1 round %2d size %u rdtsc: %lu maxnnid %d\n", round, sumsize / sizeof(uint32_t), rdtsc1 - rdtsc0, maxnnid);
        assert(maxnnid < NYZ2);
        tcounts[id] = sumsize / sizeof(uint32_t);
    }

    void trim()
    {
        if (nthreads == 1) {
            trimmer(0);
            return;
        }
        void* etworker(void* vp);
        thread_ctx* threads = new thread_ctx[nthreads];
        for (uint32_t t = 0; t < nthreads; t++) {
            threads[t].id = t;
            threads[t].et = this;
            int err = pthread_create(&threads[t].thread, NULL, etworker, (void*)&threads[t]);
            assert(err == 0);
        }
        for (uint32_t t = 0; t < nthreads; t++) {
            int err = pthread_join(threads[t].thread, NULL);
            assert(err == 0);
        }
        delete[] threads;
    }
    void barrier()
    {
        int rc = pthread_barrier_wait(&barry);
        assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
#ifdef EXPANDROUND
#define BIGGERSIZE BIGSIZE + 1
#else
#define BIGGERSIZE BIGSIZE
#define EXPANDROUND COMPRESSROUND
#endif
    void trimmer(uint32_t id)
    {
        genUnodes(id, 0);
        barrier();
        genVnodes(id, 1);
        for (uint32_t round = 2; round < ntrims - 2; round += 2) {
            barrier();
            if (round < COMPRESSROUND) {
                if (round < EXPANDROUND)
                    trimedges<BIGSIZE, BIGSIZE, true>(id, round);
                else if (round == EXPANDROUND)
                    trimedges<BIGSIZE, BIGGERSIZE, true>(id, round);
                else
                    trimedges<BIGGERSIZE, BIGGERSIZE, true>(id, round);
            } else if (round == COMPRESSROUND) {
                trimrename<BIGGERSIZE, BIGGERSIZE, true>(id, round);
            } else
                trimedges1<true>(id, round);
            barrier();
            if (round < COMPRESSROUND) {
                if (round + 1 < EXPANDROUND)
                    trimedges<BIGSIZE, BIGSIZE, false>(id, round + 1);
                else if (round + 1 == EXPANDROUND)
                    trimedges<BIGSIZE, BIGGERSIZE, false>(id, round + 1);
                else
                    trimedges<BIGGERSIZE, BIGGERSIZE, false>(id, round + 1);
            } else if (round == COMPRESSROUND) {
                trimrename<BIGGERSIZE, sizeof(uint32_t), false>(id, round + 1);
            } else
                trimedges1<false>(id, round + 1);
        }
        barrier();
        trimrename1<true>(id, ntrims - 2);
        barrier();
        trimrename1<false>(id, ntrims - 1);
    }
};

void* etworker(void* vp)
{
    thread_ctx* tp = (thread_ctx*)vp;
    tp->et->trimmer(tp->id);
    pthread_exit(NULL);
    return 0;
}

int nonce_cmp(const void* a, const void* b)
{
    return *(uint32_t*)a - *(uint32_t*)b;
}

// break circular reference with forward declaration
class solver_ctx;

typedef struct {
    uint32_t id;
    pthread_t thread;
    solver_ctx* solver;
} match_ctx;

class solver_ctx
{
public:
    edgetrimmer* trimmer;
    uint32_t* cuckoo = 0;
    proof cycleus;
    proof cyclevs;
    std::bitset<NXY> uxymap;
    std::vector<uint32_t> sols; // concatanation of all proof's indices

    solver_ctx(const char* header, const uint32_t headerlen, uint32_t difficulty, uint32_t edgeMaskIn, const uint32_t n_threads, const uint32_t n_trims, bool allrounds)
    {
        trimmer = new edgetrimmer(n_threads, n_trims, edgeMaskIn, allrounds);

        // ((uint32_t *)header)[headerlen/sizeof(uint32_t)-1] = htole32(0); // place nonce at end

        setKeys(header, headerlen, &trimmer->sip_keys);
        printf("k0 k1 %lx %lx\n", trimmer->sip_keys.k0, trimmer->sip_keys.k1);
        trimmer->InitHasher();

        cuckoo = 0;
    }

    ~solver_ctx()
    {
        delete trimmer;
    }
    uint64_t sharedbytes() const
    {
        return sizeof(matrix<ZBUCKETSIZE>);
    }
    uint32_t threadbytes() const
    {
        return sizeof(thread_ctx) + sizeof(yzbucket<TBUCKETSIZE>) + sizeof(zbucket8) + sizeof(zbucket16) + sizeof(zbucket32);
    }
    void recordedge(const uint32_t i, const uint32_t u2, const uint32_t v2)
    {
        const uint32_t u1 = u2 / 2;
        const uint32_t ux = u1 >> YZ2BITS;
        uint32_t uyz = trimmer->buckets[ux][(u1 >> Z2BITS) & YMASK].renameu1[u1 & Z2MASK];
        assert(uyz < NYZ1);
        const uint32_t v1 = v2 / 2;
        const uint32_t vx = v1 >> YZ2BITS;
        uint32_t vyz = trimmer->buckets[(v1 >> Z2BITS) & YMASK][vx].renamev1[v1 & Z2MASK];
        assert(vyz < NYZ1);
#if COMPRESSROUND > 0
        uyz = trimmer->buckets[ux][uyz >> Z1BITS].renameu[uyz & Z1MASK];
        vyz = trimmer->buckets[vyz >> Z1BITS][vx].renamev[vyz & Z1MASK];
#endif
        const uint32_t u = ((ux << YZBITS) | uyz) << 1;
        const uint32_t v = ((vx << YZBITS) | vyz) << 1 | 1;
        printf(" (%x,%x)", u, v);

        cycleus[i] = u / 2;
        cyclevs[i] = v / 2;
        uxymap[u / 2 >> ZBITS] = 1;
    }

    void solution(const uint32_t* us, uint32_t nu, const uint32_t* vs, uint32_t nv)
    {
        printf("Nodes");
        uint32_t ni = 0;
        recordedge(ni++, *us, *vs);
        while (nu--)
            recordedge(ni++, us[(nu + 1) & ~1], us[nu | 1]); // u's in even position; v's in odd
        while (nv--)
            recordedge(ni++, vs[nv | 1], vs[(nv + 1) & ~1]); // u's in odd position; v's in even
        printf("\n");

        void* matchworker(void* vp);

        sols.resize(sols.size() + PROOFSIZE);
        match_ctx* threads = new match_ctx[trimmer->nthreads];
        for (uint32_t t = 0; t < trimmer->nthreads; t++) {
            threads[t].id = t;
            threads[t].solver = this;
            int err = pthread_create(&threads[t].thread, NULL, matchworker, (void*)&threads[t]);
            assert(err == 0);
        }
        for (uint32_t t = 0; t < trimmer->nthreads; t++) {
            int err = pthread_join(threads[t].thread, NULL);
            assert(err == 0);
        }
        qsort(&sols[sols.size() - PROOFSIZE], PROOFSIZE, sizeof(uint32_t), nonce_cmp);
    }

    static const uint32_t CUCKOO_NIL = ~0;

    uint32_t path(uint32_t u, uint32_t* us) const
    {
        uint32_t nu, u0 = u;
        for (nu = 0; u != CUCKOO_NIL; u = cuckoo[u]) {
            if (nu >= MAXPATHLEN) {
                while (nu-- && us[nu] != u)
                    ;
                if (!~nu)
                    printf("maximum path length exceeded\n");
                else
                    printf("illegal %4d-cycle from node %d\n", MAXPATHLEN - nu, u0);
                pthread_exit(NULL);
            }
            us[nu++] = u;
        }
        return nu - 1;
    }

    bool findcycles()
    {
        uint32_t us[MAXPATHLEN], vs[MAXPATHLEN];
        uint64_t rdtsc0, rdtsc1;

        rdtsc0 = __rdtsc();
        for (uint32_t vx = 0; vx < NX; vx++) {
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE>& zb = trimmer->buckets[ux][vx];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/4);
                for (; readbig < endreadbig; readbig++) {
                    // bit        21..11     10...0
                    // write      UYYZZZ'    VYYZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t uxyz = (ux << YZ2BITS) | (e >> YZ2BITS);
                    const uint32_t vxyz = (vx << YZ2BITS) | (e & YZ2MASK);
                    const uint32_t u0 = uxyz << 1, v0 = (vxyz << 1) | 1;
                    if (u0 != CUCKOO_NIL) {
                        uint32_t nu = path(u0, us), nv = path(v0, vs);
                        // printf("vx %02x ux %02x e %08x uxyz %06x vxyz %06x u0 %x v0 %x nu %d nv %d\n", vx, ux, e, uxyz, vxyz, u0, v0, nu, nv);
                        if (us[nu] == vs[nv]) {
                            const uint32_t min = nu < nv ? nu : nv;
                            for (nu -= min, nv -= min; us[nu] != vs[nv]; nu++, nv++)
                                ;
                            const uint32_t len = nu + nv + 1;
                            printf("%4d-cycle found\n", len);
                            if (len == PROOFSIZE) {
                                solution(us, nu, vs, nv);
                                return true;
                            }
                        } else if (nu < nv) {
                            while (nu--)
                                cuckoo[us[nu + 1]] = us[nu];
                            cuckoo[u0] = v0;
                        } else {
                            while (nv--)
                                cuckoo[vs[nv + 1]] = vs[nv];
                            cuckoo[v0] = u0;
                        }
                    }
                }
            }
        }
        rdtsc1 = __rdtsc();
        printf("findcycles rdtsc: %lu\n", rdtsc1 - rdtsc0);

        return false;
    }

    bool solve()
    {
        assert((uint64_t)CUCKOO_SIZE * sizeof(uint32_t) <= trimmer->nthreads * sizeof(yzbucket<TBUCKETSIZE>));
        trimmer->trim();
        cuckoo = (uint32_t*)trimmer->tbuckets;
        memset(cuckoo, CUCKOO_NIL, CUCKOO_SIZE * sizeof(uint32_t));
        return findcycles();
        // return sols.size() / PROOFSIZE;
    }

    void* matchUnodes(match_ctx* mc)
    {
        uint64_t rdtsc0, rdtsc1;

        rdtsc0 = __rdtsc();
        const uint32_t starty = NY * mc->id / trimmer->nthreads;
        const uint32_t endy = NY * (mc->id + 1) / trimmer->nthreads;
        uint32_t edge = starty << YZBITS, endedge = edge + NYZ;

        for (uint32_t my = starty; my < endy; my++, endedge += NYZ) {
            for (; edge < endedge; edge += NSIPHASH) {
// bit        28..21     20..13    12..0
// node       XXXXXX     YYYYYY    ZZZZZ
#if NSIPHASH == 1
                const uint32_t nodeu = _sipnode(&trimmer->sip_keys, trimmer->edgeMask, edge, 0);
                if (uxymap[nodeu >> ZBITS]) {
                    for (uint32_t j = 0; j < PROOFSIZE; j++) {
                        if (cycleus[j] == nodeu && cyclevs[j] == _sipnode(&trimmer->sip_keys, trimmer->edgeMask, edge, 1)) {
                            sols[sols.size() - PROOFSIZE + j] = edge;
                        }
                    }
                }
// bit        39..21     20..13    12..0
// write        edge     YYYYYY    ZZZZZ

#else
#error not implemented
#endif
            }
        }
        rdtsc1 = __rdtsc();
        if (trimmer->showall || !mc->id) printf("matchUnodes id %d rdtsc: %lu\n", mc->id, rdtsc1 - rdtsc0);
        pthread_exit(NULL);
        return 0;
    }
};

bool FindCycleAdvanced(const uint256& hash, uint8_t nodesBits, uint8_t edgesRatio, uint8_t proofSize, std::set<uint32_t>& cycle)
{
    nodesBits = EDGEBITS + 1;
    assert(edgesRatio >= 0 && edgesRatio <= 100);
    assert(nodesBits <= 32);

    uint32_t nodesCount = 1 << (nodesBits - 1);
    // edge mask is a max valid value of an edge.
    uint32_t edgeMask = nodesCount - 1;

    uint32_t difficulty = edgesRatio * (uint64_t)nodesCount / 100;

    uint8_t nthreads = 8;
    uint32_t ntrims = nodesBits > 31 ? 96 : 68;
    bool allrounds = false;

    printf("Looking for %d-cycle on cuckoo%d(\"%s\") with 50%% edges\n", proofSize, nodesBits, hash.GetHex().c_str());

    solver_ctx ctx(hash.GetHex().c_str(), hash.GetHex().size(), difficulty, edgeMask, nthreads, ntrims, allrounds);

    uint64_t sbytes = ctx.sharedbytes();
    uint32_t tbytes = ctx.threadbytes();

    int sunit, tunit;
    for (sunit = 0; sbytes >= 10240; sbytes >>= 10, sunit++)
        ;
    for (tunit = 0; tbytes >= 10240; tbytes >>= 10, tunit++)
        ;
    printf("Using %d%cB bucket memory at %lx,\n", sbytes, " KMGT"[sunit], (uint64_t)ctx.trimmer->buckets);
    printf("%dx%d%cB thread memory at %lx,\n", nthreads, tbytes, " KMGT"[tunit], (uint64_t)ctx.trimmer->tbuckets);
    printf("%d-way siphash, and %d buckets.\n", NSIPHASH, NX);

    uint32_t timems;
    struct timeval time0, time1;

    gettimeofday(&time0, 0);

    bool found = ctx.solve();

    gettimeofday(&time1, 0);
    timems = (time1.tv_sec - time0.tv_sec) * 1000 + (time1.tv_usec - time0.tv_usec) / 1000;
    printf("Time: %d ms\n", timems);

    if (found) {
        copy(ctx.sols.begin(), ctx.sols.begin() + ctx.sols.size(), inserter(cycle, cycle.begin()));
    }

    // for (unsigned s = 0; s < 1; s++) {
    //     printf("Solution");
    //     uint32_t* prf = &ctx.sols[s * proofSize];
    //     for (uint32_t i = 0; i < proofSize; i++)
    //         printf(" %jx", (uintmax_t)prf[i]);
    //     printf("\n");
    // }


    return found;
}

void* matchworker(void* vp)
{
    match_ctx* tp = (match_ctx*)vp;
    tp->solver->matchUnodes(tp);
    pthread_exit(NULL);
    return 0;
}
