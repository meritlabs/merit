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

#define PROOFSIZE 42

// but they may need syncing entries
#if BIGSIZE0 == 4 && EDGEBITS > 27
#define NEEDSYNC
#endif

typedef uint8_t uint8_t;
typedef uint16_t uint16_t;

typedef uint32_t offset_t;

#if BIGSIZE0 > 4
typedef uint64_t BIGTYPE0;
#else
typedef uint32_t BIGTYPE0;
#endif

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

struct Params {
    const uint32_t edgeMask;
    const uint8_t edgeBits;
    const uint8_t xBits;
    const uint8_t yBits;
    const uint8_t zBits;
    const uint8_t nThreads;
    // node bits have two groups of bucketbits (X for big and Y for small) and a remaining group Z of degree bits
    const uint32_t nX;
    const uint32_t xMask;
    const uint32_t nY;
    const uint32_t yMask;
    const uint32_t xyBits;
    const uint32_t nXY;
    const uint32_t nZ;
    const uint32_t zMask;
    const uint8_t yzBits;
    const uint32_t nYZ;
    const uint32_t yzMask;
    const uint32_t yz1Bits; // compressed YZ bits
    const uint32_t nYZ1;
    const uint32_t yz1Mask;
    const uint32_t z1Bits;
    const uint32_t nZ1;
    const uint32_t z1Mask;
    const uint32_t yz2Bits; // more compressed YZ bits
    const uint32_t nYZ2;
    const uint32_t yz2Mask;
    const uint32_t z2Bits;
    const uint32_t nZ2;
    const uint32_t z2Mask;
    const uint32_t yzzBits;
    const uint32_t yzz1Bits;

    const uint32_t bigSlotBits;
    const uint32_t smallSlotBits;
    const uint64_t bigSlotMask;
    const uint64_t smallSlotMask;
    const uint32_t bigSlotBits0;
    const uint64_t bigSlotMask0;
    const uint32_t nonYZBits;
    const uint32_t nNonYZ;

    const uint8_t bigSize;
    const uint8_t bigSize0;
    const uint8_t biggerSize;
    const uint8_t smallSize;
    const uint8_t compressRounds;
    const uint8_t expandRounds;

    const uint32_t nTrimmedZ;
    const uint32_t zBucketSlots;
    const uint32_t zBucketSize;
    const uint32_t tBucketSize;


    constexpr Params(uint8_t edgeBitsIn, uint8_t xBitsIn, uint8_t yBitsIn, uint8_t zBitsIn, uint8_t nThreadsIn) :
    nThreads{nThreadsIn},
    edgeMask{(uint32_t)((1 << edgeBits) - 1)},
    edgeBits{edgeBitsIn},
    xBits{xBitsIn},
    yBits{yBitsIn},
    zBits{zBitsIn},
    bigSize{(uint8_t)(edgeBits <= 15 ? 4 : 5)},
    bigSize0{(uint8_t)(edgeBits < 30 ? 4 : bigSize)},
    biggerSize{bigSize},
    smallSize{bigSize},
    compressRounds{(uint8_t)(edgeBits <= 15 ? 0 : 14)},
    expandRounds{compressRounds},
    nX{(uint32_t)(1 << xBits)},
    xMask{(uint32_t)(nX - 1)},
    nY{(uint32_t)(1 << yBits)},
    yMask{(uint32_t)(nY - 1)},
    nZ{(uint32_t)(1 << zBits)},
    zMask{(uint32_t)(nZ - 1)},
    xyBits{(uint32_t)(xBits + yBits)},
    nXY{(uint32_t)(1 << xyBits)},
    yzBits{(uint8_t)(edgeBits - xBits)},
    nYZ{(uint32_t)(1 << yzBits)},
    yzMask{(uint32_t)(nYZ - 1)},
    yz1Bits{(uint8_t)(yzBits < 15 ? yzBits : 15)},
    nYZ1{(uint32_t)(1 << yz1Bits)},
    yz1Mask{(uint32_t)(nYZ1 - 1)},
    z1Bits{(uint8_t)(yz1Bits - yBits)},

    nZ1{(uint32_t)(1 << z1Bits)},
    z1Mask{(uint32_t)(nZ1 - 1)},

    yz2Bits{(uint8_t)(yzBits < 11 ? yzBits : 11)}, // more compressed YZ bits
    nYZ2{(uint32_t)(1 << yz2Bits)},
    yz2Mask{(uint32_t)(nYZ2 - 1)},

    z2Bits{(uint8_t)(yz2Bits - yBits)},
    nZ2{(uint32_t)(1 << z2Bits)},
    z2Mask{(uint32_t)(nZ2 - 1)},

    yzzBits{(uint8_t)(yzBits + zBits)},
    yzz1Bits{(uint8_t)(yz1Bits + zBits)},

    bigSlotBits{(uint32_t)(bigSize * 8)},
    smallSlotBits{(uint32_t)(smallSize * 8)},
    bigSlotMask{(uint32_t)((1ULL << bigSlotBits) - 1ULL)},
    smallSlotMask{(uint32_t)((1ULL << smallSlotBits) - 1ULL)},
    bigSlotBits0{(uint32_t)(bigSize0 * 8)},
    bigSlotMask0{(uint32_t)((1ULL << bigSlotBits0) - 1ULL)},

    nonYZBits{(uint32_t)(bigSlotBits0 - yzBits)},
    nNonYZ{(uint32_t)(1 << nonYZBits)},

    nTrimmedZ{(uint32_t)(nZ * TRIMFRAC256 / 256)},
    zBucketSlots{(uint32_t)(nZ + nZ * BIGEPS)},
    zBucketSize{(uint32_t)(zBucketSlots * bigSize0)},
    tBucketSize{(uint32_t)(zBucketSlots * bigSize)}
    {
    }
};

// typedef uint32_t BIGTYPE0;

// typedef uint32_t offset_t;

typedef uint32_t proof[PROOFSIZE];


// grow with cube root of size, hardly affected by trimming
// const static uint32_t CUCKOO_SIZE = 2 * NX * NYZ2;
const static uint32_t CUCKOO_SIZE = 2 * 128 * 128;

template <uint32_t BUCKETSIZE, uint32_t NZ1, uint32_t NZ2, uint32_t COMPRESSROUND>
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

template <uint32_t BUCKETSIZE, uint32_t NZ1, uint32_t NZ2, uint32_t COMPRESSROUND, uint8_t NY>
using yzbucket = zbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND>[NY];

template <uint32_t BUCKETSIZE, uint32_t NZ1, uint32_t NZ2, uint32_t COMPRESSROUND, uint8_t NY, uint8_t NX>
using matrix = yzbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>[NX];

template <uint32_t BUCKETSIZE, uint32_t NZ1, uint32_t NZ2, uint32_t COMPRESSROUND, uint8_t NX, uint8_t NY>
struct indexer {
    offset_t index[NX]; // uint32_t[128] - array of addresses in trimmer->buckets matrix row or column

    void matrixv(const uint32_t y)
    {
        const yzbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX>* foo = 0;
        for (uint32_t x = 0; x < NX; x++)
            index[x] = foo[x][y].bytes - (uint8_t*)foo;
    }
    offset_t storev(yzbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX>* buckets, const uint32_t y)
    {
        uint8_t const* base = (uint8_t*)buckets;
        offset_t sumsize = 0;
        for (uint32_t x = 0; x < NX; x++) {
            sumsize += buckets[x][y].setsize(base + index[x]);
        }
        return sumsize;
    }
    void matrixu(const uint32_t x)
    {
        const yzbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX>* foo = 0;
        for (uint32_t y = 0; y < NY; y++)
            index[y] = foo[x][y].bytes - (uint8_t*)foo;
    }
    offset_t storeu(yzbucket<BUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX>* buckets, const uint32_t x)
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

template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND>
class edgetrimmer; // avoid circular references

template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND>
struct thread_ctx {
    uint32_t id;
    pthread_t thread;
    edgetrimmer<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>* et;
};

template <uint8_t NYZ1>
using zbucket8 = uint8_t[2 * NYZ1];

template <uint8_t NTRIMMEDZ>
using zbucket16 = uint16_t[NTRIMMEDZ];

template <uint8_t NTRIMMEDZ>
using zbucket32 = uint32_t[NTRIMMEDZ];

// maintains set of trimmable edges
template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND>
class edgetrimmer
{
public:
    siphash_keys sip_keys;
    yzbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>* buckets;
    yzbucket<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>* tbuckets;
    zbucket32<NTRIMMEDZ>* tedges;
    zbucket16<NTRIMMEDZ>* tzs;
    zbucket8<NYZ1>* tdegs;
    offset_t* tcounts;
    uint32_t ntrims;
    uint8_t nthreads;
    pthread_barrier_t barry;
    uint8_t nodesBits;
    uint32_t difficulty;
    uint32_t edgeMask;

    Params params;

    void touch(uint8_t* p, const offset_t n)
    {
        for (offset_t i = 0; i < n; i += 4096)
            *(uint32_t*)(p + i) = 0;
    }

    edgetrimmer(const uint8_t nThreadsIn, const uint32_t nTrimsIn, Params paramsIn)
    {
        // assert(sizeof(matrix<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY, NX>) == params.nX * sizeof(yzbucket<params.zBucketSize, params.nZ1, params.nZ2, params.compressRounds, params.nX>));
        // assert(sizeof(matrix<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY, NX>) == params.nX * sizeof(yzbucket<params.tBucketSize, params.nZ1, params.nZ2, params.compressRounds, params.nX>));

        nthreads = nThreadsIn;
        ntrims = nTrimsIn;

        params = paramsIn;

        buckets = new yzbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>[NX];
        touch((uint8_t*)buckets, sizeof(matrix<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY, NX>));
        tbuckets = new yzbucket<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>[nthreads];
        touch((uint8_t*)tbuckets, nthreads * sizeof(yzbucket<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>));

        tedges = new zbucket32<NTRIMMEDZ>[nthreads];
        tdegs = new zbucket8<NYZ1>[nthreads];
        tzs = new zbucket16<NTRIMMEDZ>[nthreads];
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

    // Generate all nodes and store it in a buckets for further processing
    // There's no notion of odd/even node in the process of generating nodes (why?)
    void genUnodes(const uint32_t id, const uint32_t uorv)
    {
        uint64_t rdtsc0, rdtsc1;
        rdtsc0 = __rdtsc();

        uint8_t const* base = (uint8_t*)buckets;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;
        const uint32_t starty = NY * id / nthreads;     // 0 for nthreads = 1
        const uint32_t endy = NY * (id + 1) / nthreads; // 128 for nthreads = 1
        uint32_t edge = starty << params.yzBits;        // 0 as starty is 0
        uint32_t endedge = edge + params.nYZ;           // 0 + 2^(7 + 13) = 1 048 576

        // printf("starty: %d; endy: %d\n", starty, endy);
        // printf("edge: %d; endedge: %d\n", edge, endedge);

        offset_t sumsize = 0;
        for (uint32_t my = starty; my < endy; my++, endedge += params.nYZ) {
            dst.matrixv(my);

            // printf("my: %d; endedge: %d\n", my, endedge);

            // edge is a "nonce" for sipnode()
            for (; edge < endedge; edge += NSIPHASH) {
// bit        28..21     20..13    12..0
// node       XXXXXX     YYYYYY    ZZZZZ
#if NSIPHASH == 1
                const uint32_t node = _sipnode(&sip_keys, edgeMask, edge, uorv); // node - generated random node for the graph

                const uint32_t ux = node >> params.yzBits;                                    // ux - highest X (7) bits
                const BIGTYPE0 zz = (BIGTYPE0)edge << params.yzBits | (node & params.yzMask); // - edge YYYYYY ZZZZZ

                if (edge % 100000 == 0) {
                    // printf("edge: %x, node: %7x; ux: %2x, zz: %8x, dst.index[ux]: %8x\n", edge, node, ux, zz, dst.index[ux]);
                }
                // bit        39..21     20..13    12..0
                // write        edge     YYYYYY    ZZZZZ
                *(BIGTYPE0*)(base + dst.index[ux]) = zz;
                dst.index[ux] += params.bigSize0;

#else
#error not implemented
#endif
            }
            sumsize += dst.storev(buckets, my);
        }
        rdtsc1 = __rdtsc();
        // if (!id) printf("genUnodes round %2d size %u rdtsc: %lu\n", uorv, sumsize / params.bigSize0, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / params.bigSize0;
    }

    // Porcess butckets and discard nodes with one edge for it (means it won't be in a cycle)
    // Generate new paired nodes for remaining nodes generated in genUnodes step
    void genVnodes(const uint32_t id, const uint32_t uorv)
    {
        uint64_t rdtsc0, rdtsc1;

        static const uint32_t NONDEGBITS = std::min(params.bigSlotBits, 2 * params.yzBits) - params.zBits; // 28
        static const uint32_t NONDEGMASK = (1 << NONDEGBITS) - 1;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;
        indexer<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> small;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startux = params.nX * id / nthreads;
        const uint32_t endux = params.nX * (id + 1) / nthreads;

        for (uint32_t ux = startux; ux < endux; ux++) { // matrix x == ux
            small.matrixu(0);
            for (uint32_t my = 0; my < params.nY; my++) {
                uint32_t edge = my << params.yzBits;
                uint8_t* readbig = buckets[ux][my].bytes;
                uint8_t const* endreadbig = readbig + buckets[ux][my].size;
                // printf("id %d x %d y %d size %u read %d\n", id, ux, my, buckets[ux][my].size, readbig-base);
                for (; readbig < endreadbig; readbig += params.bigSize0) {
                    // bit     39/31..21     20..13    12..0
                    // read         edge     UYYYYY    UZZZZ   within UX partition
                    BIGTYPE0 e = *(BIGTYPE0*)readbig;
                    // restore edge generated in genUnodes
                    edge += ((uint32_t)(e >> params.yzBits) - edge) & (params.nNonYZ - 1);
                    // if (ux==78 && my==243) printf("id %d ux %d my %d e %08x prefedge %x edge %x\n", id, ux, my, e, e >> params.yzBits, edge);
                    const uint32_t uy = (e >> params.zBits) & params.yMask;
                    // bit         39..13     12..0
                    // write         edge     UZZZZ   within UX UY partition
                    *(uint64_t*)(small0 + small.index[uy]) = ((uint64_t)edge << params.zBits) | (e & params.zMask);
                    // printf("id %d ux %d y %d e %010lx e' %010x\n", id, ux, my, e, ((uint64_t)edge << ZBITS) | (e >> YBITS));
                    small.index[uy] += params.smallSize;
                }
                if (unlikely(edge >> params.nonYZBits != (((my + 1) << params.yzBits) - 1) >> params.nonYZBits)) {
                    printf("OOPS1: id %d ux %d y %d edge %x vs %x\n", id, ux, my, edge, ((my + 1) << params.yzBits) - 1);
                    exit(0);
                }
            }
            // counts of zz's for this ux
            uint8_t* degs = tdegs[id];
            small.storeu(tbuckets + id, 0);
            dst.matrixu(ux);
            for (uint32_t uy = 0; uy < params.nY; uy++) {
                // set to FF. FF + 1 gives zero! (why not just zero, and then check for degs[z] == 1 ???)
                memset(degs, 0xff, params.nZ);
                uint8_t *readsmall = tbuckets[id][uy].bytes, *endreadsmall = readsmall + tbuckets[id][uy].size;
                // if (id==1) printf("id %d ux %d y %d size %u sumsize %u\n", id, ux, uy, tbuckets[id][uy].size/params.bigSize, sumsize);

                // go through all Z'a values and store count for each ZZ in degs[] array (initial value + 1 gives 0 (!))
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += params.smallSize) {
                    degs[*(uint32_t*)rdsmall & params.zMask]++;
                }

                uint16_t* zs = tzs[id];
                uint32_t* edges0 = tedges[id]; // list of nodes with 2+ edges
                uint32_t *edges = edges0, edge = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += params.smallSize) {
                    // bit         39..13     12..0
                    // read          edge     UZZZZ    sorted by UY within UX partition
                    const uint64_t e = *(uint64_t*)rdsmall;
                    // restore edge value ( how ??? )
                    edge += ((e >> params.zBits) - edge) & NONDEGMASK;
                    // if (id==0) printf("id %d ux %d uy %d e %010lx pref %4x edge %x mask %x\n", id, ux, uy, e, e>>ZBITS, edge, NONDEGMASK);
                    *edges = edge;
                    const uint32_t z = e & params.zMask;
                    *zs = z;

                    // check if array of ZZs counts (degs[]) has value not equal to 0 (means we have one edge for that node)
                    // if it's the only edge, then it would be rewritten in zs and edges arrays in next iteration (skipped)
                    const uint32_t delta = degs[z] ? 1 : 0;
                    edges += delta;
                    zs += delta;
                }
                if (unlikely(edge >> NONDEGBITS != params.edgeMask >> NONDEGBITS)) {
                    printf("OOPS2: id %d ux %d uy %d edge %x vs %x\n", id, ux, uy, edge, params.edgeMask);
                    exit(0);
                }
                assert(edges - edges0 < NTRIMMEDZ);
                const uint16_t* readz = tzs[id];
                const uint32_t* readedge = edges0;
                int64_t uy34 = (int64_t)uy << params.yzzBits;

                for (; readedge < edges; readedge++, readz++) { // process up to 7 leftover edges if NSIPHASH==8
                    const uint32_t node = _sipnode(&sip_keys, edgeMask, *readedge, uorv);
                    const uint32_t vx = node >> params.yzBits; // & XMASK;

                    // bit        39..34    33..21     20..13     12..0
                    // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition
                    // prev bucket info generated in genUnodes is overwritten here,
                    // as we store U and V nodes in one value (Yz and Zs; Xs are indices in a matrix)
                    // edge is discarded here, as we do not need it anymore
                    // QUESTION: why do not we use even/odd nodes generation in this algo?
                    *(uint64_t*)(base + dst.index[vx]) = uy34 | ((uint64_t)*readz << params.yzBits) | (node & params.yzMask);
                    // printf("id %d ux %d y %d edge %08x e' %010lx vx %d\n", id, ux, uy, *readedge, uy34 | ((uint64_t)(node & params.yzMask) << params.zBits) | *readz, vx);
                    dst.index[vx] += params.bigSize;
                }
            }
            // printf("sumsize: %d\n", sumsize);
            sumsize += dst.storeu(buckets, ux);
        }
        rdtsc1 = __rdtsc();
        // if (!id) printf("genVnodes round %2d size %u rdtsc: %lu\n", uorv, sumsize / params.bigSize, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / params.bigSize;
    }

    template <uint32_t SRCSIZE, uint32_t DSTSIZE, bool TRIMONV>
    void trimedges(const uint32_t id, const uint32_t round)
    {
        const uint32_t SRCSLOTBITS = std::min(SRCSIZE * 8, 2 * params.yzBits);
        const uint64_t SRCSLOTMASK = (1ULL << SRCSLOTBITS) - 1ULL;
        const uint32_t SRCPREFBITS = SRCSLOTBITS - params.yzBits;
        const uint32_t SRCPREFMASK = (1 << SRCPREFBITS) - 1;
        const uint32_t DSTSLOTBITS = std::min(DSTSIZE * 8, 2 * params.yzBits);
        const uint64_t DSTSLOTMASK = (1ULL << DSTSLOTBITS) - 1ULL;
        const uint32_t DSTPREFBITS = DSTSLOTBITS - params.yzzBits;
        const uint32_t DSTPREFMASK = (1 << DSTPREFBITS) - 1;
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;
        indexer<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> small;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startvx = params.nY * id / nthreads;
        const uint32_t endvx = params.nY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            small.matrixu(0);
            for (uint32_t ux = 0; ux < params.nX; ux++) {
                uint32_t uxyz = ux << params.yzBits;
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                const uint8_t *readbig = zb.bytes, *endreadbig = readbig + zb.size;
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig += SRCSIZE) {
                    // bit        39..34    33..21     20..13     12..0
                    // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition
                    const uint64_t e = *(uint64_t*)readbig & SRCSLOTMASK;
                    uxyz += ((uint32_t)(e >> params.yzBits) - uxyz) & SRCPREFMASK;
                    // if (round==6) printf("id %d vx %d ux %d e %010lx suffUXYZ %05x suffUXY %03x UXYZ %08x UXY %04x mask %x\n", id, vx, ux, e, (uint32_t)(e >> params.yzBits), (uint32_t)(e >> YZZBITS), uxyz, uxyz>>ZBITS, SRCPREFMASK);
                    const uint32_t vy = (e >> params.zBits) & params.yMask;
                    // bit     41/39..34    33..26     25..13     12..0
                    // write      UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    *(uint64_t*)(small0 + small.index[vy]) = ((uint64_t)uxyz << params.zBits) | (e & params.zMask);
                    uxyz &= ~params.zMask;
                    small.index[vy] += DSTSIZE;
                }
                if (unlikely(uxyz >> params.yzBits != ux)) {
                    printf("OOPS3: id %d vx %d ux %d UXY %x\n", id, vx, ux, uxyz);
                    exit(0);
                }
            }
            uint8_t* degs = tdegs[id];
            small.storeu(tbuckets + id, 0);
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            for (uint32_t vy = 0; vy < params.nY; vy++) {
                const uint64_t vy34 = (uint64_t)vy << params.yzzBits;
                memset(degs, 0xff, params.nZ);
                uint8_t *readsmall = tbuckets[id][vy].bytes, *endreadsmall = readsmall + tbuckets[id][vy].size;
                // printf("id %d vx %d vy %d size %u sumsize %u\n", id, vx, vy, tbuckets[id][vx].size/params.bigSize, sumsize);
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += DSTSIZE)
                    degs[*(uint32_t*)rdsmall & params.zMask]++;
                uint32_t ux = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += DSTSIZE) {
                    // bit     41/39..34    33..26     25..13     12..0
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    // bit        39..37    36..30     29..15     14..0      with XBITS==YBITS==7
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition
                    const uint64_t e = *(uint64_t*)rdsmall & DSTSLOTMASK;
                    ux += ((uint32_t)(e >> params.yzzBits) - ux) & DSTPREFMASK;
                    // printf("id %d vx %d vy %d e %010lx suffUX %02x UX %x mask %x\n", id, vx, vy, e, (uint32_t)(e >> YZZBITS), ux, SRCPREFMASK);
                    // bit    41/39..34    33..21     20..13     12..0
                    // write     VYYYYY    VZZZZZ     UYYYYY     UZZZZ   within UX partition
                    *(uint64_t*)(base + dst.index[ux]) = vy34 | ((e & params.zMask) << params.yzBits) | ((e >> params.zBits) & params.yzMask);
                    dst.index[ux] += degs[e & params.zMask] ? DSTSIZE : 0;
                }
                if (unlikely(ux >> DSTPREFBITS != params.xMask >> DSTPREFBITS)) {
                    printf("OOPS4: id %d vx %x ux %x vs %x\n", id, vx, ux, params.xMask);
                }
            }
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        // if (showall || (!id && !(round & (round + 1))))
        // printf("trimedges round %2d size %u rdtsc: %lu\n", round, sumsize / DSTSIZE, rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / DSTSIZE;
    }

    template <uint32_t SRCSIZE, uint32_t DSTSIZE, bool TRIMONV>
    void trimrename(const uint32_t id, const uint32_t round)
    {
        const uint32_t SRCSLOTBITS = std::min(SRCSIZE * 8, (TRIMONV ? params.yzBits : params.yz1Bits) + params.yzBits);
        const uint64_t SRCSLOTMASK = (1ULL << SRCSLOTBITS) - 1ULL;
        const uint32_t SRCPREFBITS = SRCSLOTBITS - params.yzBits;
        const uint32_t SRCPREFMASK = (1 << SRCPREFBITS) - 1;
        const uint32_t SRCPREFBITS2 = SRCSLOTBITS - params.yzzBits;
        const uint32_t SRCPREFMASK2 = (1 << SRCPREFBITS2) - 1;
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;
        indexer<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> small;
        static uint32_t maxnnid = 0;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t const* base = (uint8_t*)buckets;
        uint8_t const* small0 = (uint8_t*)tbuckets[id];
        const uint32_t startvx = params.nY * id / nthreads;
        const uint32_t endvx = params.nY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            small.matrixu(0);
            for (uint32_t ux = 0; ux < params.nX; ux++) {
                uint32_t uyz = 0;
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                const uint8_t *readbig = zb.bytes, *endreadbig = readbig + zb.size;
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig += SRCSIZE) {
                    // bit        39..37    36..22     21..15     14..0
                    // write      UYYYYY    UZZZZZ     VYYYYY     VZZZZ   within VX partition  if TRIMONV
                    // bit            36...22     21..15     14..0
                    // write          VYYYZZ'     UYYYYY     UZZZZ   within UX partition  if !TRIMONV
                    const uint64_t e = *(uint64_t*)readbig & SRCSLOTMASK;
                    if (TRIMONV)
                        uyz += ((uint32_t)(e >> params.yzBits) - uyz) & SRCPREFMASK;
                    else
                        uyz = e >> params.yzBits;
                    // if (round==32 && ux==25) printf("id %d vx %d ux %d e %010lx suffUXYZ %05x suffUXY %03x UXYZ %08x UXY %04x mask %x\n", id, vx, ux, e, (uint32_t)(e >> params.yzBits), (uint32_t)(e >> YZZBITS), uxyz, uxyz>>ZBITS, SRCPREFMASK);
                    const uint32_t vy = (e >> params.zBits) & params.yMask;
                    // bit        39..37    36..30     29..15     14..0
                    // write      UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition  if TRIMONV
                    // bit            36...30     29...15     14..0
                    // write          VXXXXXX     VYYYZZ'     UZZZZ   within UX UY partition  if !TRIMONV
                    *(uint64_t*)(small0 + small.index[vy]) = ((uint64_t)(ux << (TRIMONV ? params.yzBits : params.yz1Bits) | uyz) << params.zBits) | (e & params.zMask);
                    // if (TRIMONV&&vx==75&&vy==83) printf("id %d vx %d vy %d e %010lx e15 %x ux %x\n", id, vx, vy, ((uint64_t)uxyz << ZBITS) | (e & ZMASK), uxyz, uxyz>>params.yzBits);
                    if (TRIMONV)
                        uyz &= ~params.zMask;
                    small.index[vy] += SRCSIZE;
                }
            }
            uint16_t* degs = (uint16_t*)tdegs[id];
            small.storeu(tbuckets + id, 0);
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            uint32_t newnodeid = 0;
            uint32_t* renames = TRIMONV ? buckets[0][vx].renamev : buckets[vx][0].renameu;
            uint32_t* endrenames = renames + params.nZ1;
            for (uint32_t vy = 0; vy < params.nY; vy++) {
                memset(degs, 0xff, 2 * params.nZ);
                uint8_t *readsmall = tbuckets[id][vy].bytes, *endreadsmall = readsmall + tbuckets[id][vy].size;
                // printf("id %d vx %d vy %d size %u sumsize %u\n", id, vx, vy, tbuckets[id][vx].size/params.bigSize, sumsize);
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE)
                    degs[*(uint32_t*)rdsmall & params.zMask]++;
                uint32_t ux = 0;
                uint32_t nrenames = 0;
                for (uint8_t* rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE) {
                    // bit        39..37    36..30     29..15     14..0
                    // read       UXXXXX    UYYYYY     UZZZZZ     VZZZZ   within VX VY partition  if TRIMONV
                    // bit            36...30     29...15     14..0
                    // read           VXXXXXX     VYYYZZ'     UZZZZ   within UX UY partition  if !TRIMONV
                    const uint64_t e = *(uint64_t*)rdsmall & SRCSLOTMASK;
                    if (TRIMONV)
                        ux += ((uint32_t)(e >> params.yzzBits) - ux) & SRCPREFMASK2;
                    else
                        ux = e >> params.yzz1Bits;
                    const uint32_t vz = e & params.zMask;
                    uint16_t vdeg = degs[vz];
                    // if (TRIMONV&&vx==75&&vy==83) printf("id %d vx %d vy %d e %010lx e37 %x ux %x vdeg %d nrenames %d\n", id, vx, vy, e, e>>params.yzzBits, ux, vdeg, nrenames);
                    if (vdeg) {
                        if (vdeg < 32) {
                            degs[vz] = vdeg = 32 + nrenames++;
                            *renames++ = vy << params.zBits | vz;
                            if (renames == endrenames) {
                                endrenames += (TRIMONV ? sizeof(yzbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>) : sizeof(zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>)) / sizeof(uint32_t);
                                renames = endrenames - params.nZ1;
                            }
                        }
                        // bit       36..22     21..15     14..0
                        // write     VYYZZ'     UYYYYY     UZZZZ   within UX partition  if TRIMONV
                        if (TRIMONV)
                            *(uint64_t*)(base + dst.index[ux]) = ((uint64_t)(newnodeid + vdeg - 32) << params.yzBits) | ((e >> params.zBits) & params.yzMask);
                        else
                            *(uint32_t*)(base + dst.index[ux]) = ((newnodeid + vdeg - 32) << params.yz1Bits) | ((e >> params.zBits) & params.yz1Mask);
                        // if (vx==44&&vy==58) printf("  id %d vx %d vy %d newe %010lx\n", id, vx, vy, vy28 | ((vdeg) << params.yzBits) | ((e >> params.zBits) & params.yzMask));
                        dst.index[ux] += DSTSIZE;
                    }
                }
                newnodeid += nrenames;
                if (TRIMONV && unlikely(ux >> SRCPREFBITS2 != params.xMask >> SRCPREFBITS2)) {
                    printf("OOPS6: id %d vx %d vy %d ux %x vs %x\n", id, vx, vy, ux, params.xMask);
                    exit(0);
                }
            }
            if (newnodeid > maxnnid)
                maxnnid = newnodeid;
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        // if (showall || !id) printf("trimrename round %2d size %u rdtsc: %lu maxnnid %d\n", round, sumsize / DSTSIZE, rdtsc1 - rdtsc0, maxnnid);
        assert(maxnnid < params.nYZ1);
        tcounts[id] = sumsize / DSTSIZE;
    }

    template <bool TRIMONV>
    void trimedges1(const uint32_t id, const uint32_t round)
    {
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint8_t* degs = tdegs[id];
        uint8_t const* base = (uint8_t*)buckets;
        const uint32_t startvx = params.nY * id / nthreads;
        const uint32_t endvx = params.nY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            memset(degs, 0xff, params.nYZ1);
            for (uint32_t ux = 0; ux < params.nX; ux++) {
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %d\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig++)
                    degs[*readbig & params.yz1Mask]++;
            }
            for (uint32_t ux = 0; ux < NX; ux++) {
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                for (; readbig < endreadbig; readbig++) {
                    // bit       29..22    21..15     14..7     6..0
                    // read      UYYYYY    UZZZZ'     VYYYY     VZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t vyz = e & params.yz1Mask;
                    // printf("id %d vx %d ux %d e %08lx vyz %04x uyz %04x\n", id, vx, ux, e, vyz, e >> params.yz1Bits);
                    // bit       29..22    21..15     14..7     6..0
                    // write     VYYYYY    VZZZZ'     UYYYY     UZZ'   within UX partition
                    *(uint32_t*)(base + dst.index[ux]) = (vyz << params.yz1Bits) | (e >> params.yz1Bits);
                    dst.index[ux] += degs[vyz] ? sizeof(uint32_t) : 0;
                }
            }
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        // if (showall || (!id && !(round & (round + 1))))
        // printf("trimedges1 round %2d size %u rdtsc: %lu\n", round, sumsize / sizeof(uint32_t), rdtsc1 - rdtsc0);
        tcounts[id] = sumsize / sizeof(uint32_t);
    }

    template <bool TRIMONV>
    void trimrename1(const uint32_t id, const uint32_t round)
    {
        uint64_t rdtsc0, rdtsc1;
        indexer<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NX, NY> dst;
        static uint32_t maxnnid = 0;

        rdtsc0 = __rdtsc();
        offset_t sumsize = 0;
        uint16_t* degs = (uint16_t*)tdegs[id];
        uint8_t const* base = (uint8_t*)buckets;
        const uint32_t startvx = params.nY * id / nthreads;
        const uint32_t endvx = params.nY * (id + 1) / nthreads;
        for (uint32_t vx = startvx; vx < endvx; vx++) {
            TRIMONV ? dst.matrixv(vx) : dst.matrixu(vx);
            memset(degs, 0xff, 2 * params.nYZ1); // sets each uint16_t entry to 0xffff
            for (uint32_t ux = 0; ux < params.nX; ux++) {
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %d\n", id, vx, ux, zb.size/SRCSIZE);
                for (; readbig < endreadbig; readbig++)
                    degs[*readbig & params.yz1Mask]++;
            }
            uint32_t newnodeid = 0;
            uint32_t* renames = TRIMONV ? buckets[0][vx].renamev1 : buckets[vx][0].renameu1;
            uint32_t* endrenames = renames + params.nZ2;
            for (uint32_t ux = 0; ux < params.nX; ux++) {
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = TRIMONV ? buckets[ux][vx] : buckets[vx][ux];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                for (; readbig < endreadbig; readbig++) {
                    // bit       29...15     14...0
                    // read      UYYYZZ'     VYYZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t vyz = e & params.yz1Mask;
                    uint16_t vdeg = degs[vyz];
                    if (vdeg) {
                        if (vdeg < 32) {
                            degs[vyz] = vdeg = 32 + newnodeid++;
                            *renames++ = vyz;
                            if (renames == endrenames) {
                                endrenames += (TRIMONV ? sizeof(yzbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>) : sizeof(zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>)) / sizeof(uint32_t);
                                renames = endrenames - params.nZ2;
                            }
                        }
                        // bit       25...15     14...0
                        // write     VYYZZZ"     UYYZZ'   within UX partition
                        *(uint32_t*)(base + dst.index[ux]) = ((vdeg - 32) << (TRIMONV ? params.yz1Bits : params.yz2Bits)) | (e >> params.yz1Bits);
                        dst.index[ux] += sizeof(uint32_t);
                    }
                }
            }
            if (newnodeid > maxnnid)
                maxnnid = newnodeid;
            sumsize += TRIMONV ? dst.storev(buckets, vx) : dst.storeu(buckets, vx);
        }
        rdtsc1 = __rdtsc();
        // if (showall || !id) printf("trimrename1 round %2d size %u rdtsc: %lu maxnnid %d\n", round, sumsize / sizeof(uint32_t), rdtsc1 - rdtsc0, maxnnid);
        assert(maxnnid < params.nYZ2);
        tcounts[id] = sumsize / sizeof(uint32_t);
    }

    void trim()
    {
        if (nthreads == 1) {
            trimmer(0);
            return;
        }
        void* etworker(void* vp);
        thread_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>* threads = new thread_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>[nthreads];
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

    void trimmer(uint32_t id)
    {
        genUnodes(id, 0);
        barrier();
        genVnodes(id, 1);
        for (uint32_t round = 2; round < ntrims - 2; round += 2) {
            barrier();
            if (round < params.compressRounds) {
                if (round < params.expandRounds)
                    trimedges<params.bigSize, params.bigSize, true>(id, round);
                else if (round == params.expandRounds)
                    trimedges<params.bigSize, params.biggerSize, true>(id, round);
                else
                    trimedges<params.biggerSize, params.biggerSize, true>(id, round);
            } else if (round == params.compressRounds) {
                trimrename<params.biggerSize, params.biggerSize, true>(id, round);
            } else
                trimedges1<true>(id, round);
            barrier();
            if (round < params.compressRounds) {
                if (round + 1 < params.expandRounds)
                    trimedges<params.bigSize, params.bigSize, false>(id, round + 1);
                else if (round + 1 == params.expandRounds)
                    trimedges<params.bigSize, params.biggerSize, false>(id, round + 1);
                else
                    trimedges<params.biggerSize, params.biggerSize, false>(id, round + 1);
            } else if (round == params.compressRounds) {
                trimrename<params.biggerSize, sizeof(uint32_t), false>(id, round + 1);
            } else
                trimedges1<false>(id, round + 1);
        }
        barrier();
        trimrename1<true>(id, ntrims - 2);
        barrier();
        trimrename1<false>(id, ntrims - 1);
    }
};

template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND>
void* etworker(void* vp)
{
    thread_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>* tp = (thread_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>*)vp;
    tp->et->trimmer(tp->id);
    pthread_exit(NULL);
    return 0;
}

int nonce_cmp(const void* a, const void* b)
{
    return *(uint32_t*)a - *(uint32_t*)b;
}

// break circular reference with forward declaration

template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND,
    uint32_t NXY>
class solver_ctx;


template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND,
    uint32_t NXY>
struct match_ctx {
    uint32_t id;
    pthread_t thread;
    solver_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>* solver;
};

template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND,
    uint32_t NXY>
class solver_ctx
{
public:
    edgetrimmer<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>* trimmer;
    uint32_t* cuckoo = 0;
    proof cycleus;
    proof cyclevs;
    std::bitset<NXY> uxymap;
    std::vector<uint32_t> sols; // concatanation of all proof's indices

    solver_ctx(const char* header, const uint32_t headerlen, Params params, const uint32_t n_threads, const uint32_t n_trims, bool allrounds)
    {
        trimmer = new edgetrimmer<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>(n_threads, n_trims, params);

        setKeys(header, headerlen, &trimmer->sip_keys);
        printf("k0 k1 %lx %lx\n", trimmer->sip_keys.k0, trimmer->sip_keys.k1);

        cuckoo = 0;
    }

    ~solver_ctx()
    {
        delete trimmer;
    }

    uint64_t sharedbytes() const
    {
        return sizeof(matrix<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY, NX>);
    }

    uint32_t threadbytes() const
    {
        return sizeof(thread_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND>) + sizeof(yzbucket<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>) + sizeof(zbucket8<NYZ1>) + sizeof(zbucket16<NTRIMMEDZ>) + sizeof(zbucket32<NTRIMMEDZ>);
    }

    void recordedge(const uint32_t i, const uint32_t u2, const uint32_t v2)
    {
        const uint32_t u1 = u2 / 2;
        const uint32_t ux = u1 >> trimmer->params.yz2Bits;
        uint32_t uyz = trimmer->buckets[ux][(u1 >> trimmer->params.z2Bits) & trimmer->params.yMask].renameu1[u1 & trimmer->params.z2Mask];
        assert(uyz < trimmer->params.nYZ1);
        const uint32_t v1 = v2 / 2;
        const uint32_t vx = v1 >> trimmer->params.yz2Bits;
        uint32_t vyz = trimmer->buckets[(v1 >> trimmer->params.z2Bits) & trimmer->params.yMask][vx].renamev1[v1 & trimmer->params.z2Mask];
        assert(vyz < trimmer->params.nYZ1);
#if COMPRESSROUND > 0
        uyz = trimmer->buckets[ux][uyz >> trimmer->params.z1Bits].renameu[uyz & trimmer->params.z1Mask];
        vyz = trimmer->buckets[vyz >> trimmer->params.z1Bits][vx].renamev[vyz & trimmer->params.z1Mask];
#endif
        const uint32_t u = ((ux << trimmer->params.yzBits) | uyz) << 1;
        const uint32_t v = ((vx << trimmer->params.yzBits) | vyz) << 1 | 1;
        printf(" (%x,%x)", u, v);

        cycleus[i] = u / 2;
        cyclevs[i] = v / 2;
        uxymap[u / 2 >> trimmer->params.zBits] = 1;
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
        match_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>* threads = new match_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>[trimmer->nthreads];
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
                zbucket<ZBUCKETSIZE, NZ1, NZ2, COMPRESSROUND>& zb = trimmer->buckets[ux][vx];
                uint32_t *readbig = zb.words, *endreadbig = readbig + zb.size / sizeof(uint32_t);
                // printf("id %d vx %d ux %d size %u\n", id, vx, ux, zb.size/4);
                for (; readbig < endreadbig; readbig++) {
                    // bit        21..11     10...0
                    // write      UYYZZZ'    VYYZZ'   within VX partition
                    const uint32_t e = *readbig;
                    const uint32_t uxyz = (ux << trimmer->params.yz2Bits) | (e >> trimmer->params.yz2Bits);
                    const uint32_t vxyz = (vx << trimmer->params.yz2Bits) | (e & trimmer->params.yz2Mask);
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
        // printf("findcycles rdtsc: %lu\n", rdtsc1 - rdtsc0);

        return false;
    }

    bool solve()
    {
        assert((uint64_t)CUCKOO_SIZE * sizeof(uint32_t) <= trimmer->nthreads * sizeof(yzbucket<TBUCKETSIZE, NZ1, NZ2, COMPRESSROUND, NY>));
        trimmer->trim();
        cuckoo = (uint32_t*)trimmer->tbuckets;
        memset(cuckoo, CUCKOO_NIL, CUCKOO_SIZE * sizeof(uint32_t));

        return findcycles();
        // return sols.size() / PROOFSIZE;
    }

    void* matchUnodes(match_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>* mc)
    {
        uint64_t rdtsc0, rdtsc1;

        rdtsc0 = __rdtsc();
        const uint32_t starty = trimmer->params.nY * mc->id / trimmer->nthreads;
        const uint32_t endy = trimmer->params.nY * (mc->id + 1) / trimmer->nthreads;
        uint32_t edge = starty << trimmer->params.yzBits, endedge = edge + trimmer->params.nYZ;

        for (uint32_t my = starty; my < endy; my++, endedge += trimmer->params.nYZ) {
            for (; edge < endedge; edge += NSIPHASH) {
// bit        28..21     20..13    12..0
// node       XXXXXX     YYYYYY    ZZZZZ
#if NSIPHASH == 1
                const uint32_t nodeu = _sipnode(&trimmer->sip_keys, trimmer->edgeMask, edge, 0);
                if (uxymap[nodeu >> trimmer->params.zBits]) {
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
        // if (trimmer->showall || !mc->id) printf("matchUnodes id %d rdtsc: %lu\n", mc->id, rdtsc1 - rdtsc0);
        pthread_exit(NULL);
        return 0;
    }
};


template <
    uint32_t ZBUCKETSIZE,
    uint32_t TBUCKETSIZE,
    uint8_t NX,
    uint8_t NY,
    uint32_t NZ1,
    uint32_t NZ2,
    uint8_t NYZ1,
    uint32_t NTRIMMEDZ,
    uint32_t COMPRESSROUND,
    uint32_t NXY>
void* matchworker(void* vp)
{
    match_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>* tp = (match_ctx<ZBUCKETSIZE, TBUCKETSIZE, NX, NY, NZ1, NZ2, NYZ1, NTRIMMEDZ, COMPRESSROUND, NXY>*)vp;
    tp->solver->matchUnodes(tp);
    pthread_exit(NULL);

    return 0;
}



bool FindCycleAdvanced(const uint256& hash, uint8_t nodesBits, uint8_t edgesRatio, uint8_t proofSize, std::set<uint32_t>& cycle)
{
    assert(edgesRatio >= 0 && edgesRatio <= 100);
    assert(nodesBits >= 16 && nodesBits <= 32);


    // prepare params for algorithm
    const uint8_t edgeBits = nodesBits - 1;
    const uint32_t edgeMask = (uint32_t)((1 << edgeBits) - 1);
    const uint8_t xBits = 7;
    const uint8_t yBits = 7;
    const uint8_t zBits = edgeBits - xBits - yBits;
    const uint8_t nThreads = 8;
    // node bits have two groups of bucketbits (X for big and Y for small) and a remaining group Z of degree bits
    const uint32_t nX = (uint32_t)(1 << xBits);
    const uint32_t xMask = (uint32_t)(nX - 1);

    const uint32_t nY = (uint32_t)(1 << yBits);
    const uint32_t yMask = (uint32_t)(nY - 1);

    const uint32_t nZ = (uint32_t)(1 << zBits);
    const uint32_t zMask = (uint32_t)(nZ - 1);

    const uint32_t xyBits = (uint32_t)(xBits + yBits);
    const uint32_t nXY = (uint32_t)(1 << xyBits);

    const uint8_t yzBits = (uint8_t)(edgeBits - xBits);
    const uint32_t nYZ = (uint32_t)(1 << yzBits);
    const uint32_t yzMask = (uint32_t)(nYZ - 1);

    const uint32_t yz1Bits = (uint8_t)(yzBits < 15 ? yzBits : 15); // compressed YZ bits
    const uint32_t nYZ1 = (uint32_t)(1 << yz1Bits);
    const uint32_t yz1Mask = (uint32_t)(nYZ1 - 1);

    const uint32_t z1Bits = (uint8_t)(yz1Bits - yBits);
    const uint32_t nZ1 = (uint32_t)(1 << z1Bits);
    const uint32_t z1Mask = (uint32_t)(nZ1 - 1);

    const uint32_t yz2Bits = (uint8_t)(yzBits < 11 ? yzBits : 11); // more compressed YZ bits
    const uint32_t nYZ2 = (uint32_t)(1 << yz2Bits);
    const uint32_t yz2Mask = (uint32_t)(nYZ2 - 1);

    const uint32_t z2Bits = (uint8_t)(yz2Bits - yBits);
    const uint32_t nZ2 = (uint32_t)(1 << z2Bits);
    const uint32_t z2Mask = (uint32_t)(nZ2 - 1);

    const uint32_t yzzBits = (uint8_t)(yzBits + zBits);
    const uint32_t yzz1Bits = (uint8_t)(yz1Bits + zBits);

    const uint8_t bigSize = (uint8_t)(edgeBits <= 15 ? 4 : 5);
    const uint8_t bigSize0 = (uint8_t)(edgeBits < 30 ? 4 : bigSize);
    const uint8_t biggerSize = bigSize;
    const uint8_t smallSize = bigSize;

    const uint32_t bigSlotBits = (uint32_t)(bigSize * 8);
    const uint32_t smallSlotBits = (uint32_t)(smallSize * 8);
    const uint64_t bigSlotMask = (uint32_t)((1ULL << bigSlotBits) - 1ULL);
    const uint64_t smallSlotMask = (uint32_t)((1ULL << smallSlotBits) - 1ULL);
    const uint32_t bigSlotBits0 = (uint32_t)(bigSize0 * 8);
    const uint64_t bigSlotMask0 = (uint32_t)((1ULL << bigSlotBits0) - 1ULL);

    const uint32_t nonYZBits = (uint32_t)(bigSlotBits0 - yzBits);
    const uint32_t nNonYZ = (uint32_t)(1 << nonYZBits);

    const uint8_t compressRounds = (uint8_t)(edgeBits <= 15 ? 0 : 14);
    const uint8_t expandRounds = compressRounds;

    const uint32_t nTrimmedZ = (uint32_t)(nZ * TRIMFRAC256 / 256);
    const uint32_t zBucketSlots = (uint32_t)(nZ + nZ * BIGEPS);
    const uint32_t zBucketSize = (uint32_t)(zBucketSlots * bigSize0);
    const uint32_t tBucketSize = (uint32_t)(zBucketSlots * bigSize);

    uint32_t nodesCount = 1 << (nodesBits - 1);
    // edge mask is a max valid value of an edge.
    uint32_t edgeMask = nodesCount - 1;

    uint32_t difficulty = edgesRatio * (uint64_t)nodesCount / 100;

    uint8_t nthreads = 8;
    uint32_t ntrims = nodesBits > 31 ? 96 : 68;
    bool allrounds = false;

    auto hashStr = hash.GetHex();

    printf("Looking for %d-cycle on cuckoo%d(\"%s\") with 50%% edges\n", proofSize, nodesBits, hashStr.c_str());

    uint8_t edgesBits = nodesBits - 1;

    Params params{
        (uint8_t)(nodesBits - 1),
        7,
        7,
        (uint8_t)(nodesBits - 14),
        nthreads,
    };

    solver_ctx<zBucketSize,
        tBucketSize,
        nX, nY,
        nZ1, nZ2,
        nYZ1, nTrimmedZ,
        compressRounds,
        nXY>
        ctx(hashStr.c_str(), hashStr.size(), params, nthreads, ntrims, allrounds);

    uint64_t sbytes = ctx.sharedbytes();
    uint32_t tbytes = ctx.threadbytes();

    int sunit, tunit;
    for (sunit = 0; sbytes >= 10240; sbytes >>= 10, sunit++)
        ;
    for (tunit = 0; tbytes >= 10240; tbytes >>= 10, tunit++)
        ;
    printf("Using %d%cB bucket memory at %lx,\n", sbytes, " KMGT"[sunit], (uint64_t)ctx.trimmer->buckets);
    printf("%dx%d%cB thread memory at %lx,\n", nthreads, tbytes, " KMGT"[tunit], (uint64_t)ctx.trimmer->tbuckets);
    printf("%d-way siphash, and %d buckets.\n", NSIPHASH, 1 << 7);

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

    return found;
}
