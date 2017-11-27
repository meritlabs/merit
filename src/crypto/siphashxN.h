#ifndef INCLUDE_SIPHASHXN_H
#define INCLUDE_SIPHASHXN_H

#ifdef __AVX2__

#include <immintrin.h> // for _mm256_* intrinsics

#define ADD(a, b) _mm256_add_epi64(a, b)
#define XOR(a, b) _mm256_xor_si256(a, b)
#define ROTATE16 _mm256_set_epi64x(0x0D0C0B0A09080F0EULL, 0x0504030201000706ULL, \
    0x0D0C0B0A09080F0EULL, 0x0504030201000706ULL)
#define ROT13(x) _mm256_or_si256(_mm256_slli_epi64(x, 13), _mm256_srli_epi64(x, 51))
#define ROT16(x) _mm256_shuffle_epi8((x), ROTATE16)
#define ROT17(x) _mm256_or_si256(_mm256_slli_epi64(x, 17), _mm256_srli_epi64(x, 47))
#define ROT21(x) _mm256_or_si256(_mm256_slli_epi64(x, 21), _mm256_srli_epi64(x, 43))
#define ROT32(x) _mm256_shuffle_epi32((x), _MM_SHUFFLE(2, 3, 0, 1))

#define SIPROUNDX8        \
    do {                  \
        v0 = ADD(v0, v1); \
        v4 = ADD(v4, v5); \
        v2 = ADD(v2, v3); \
        v6 = ADD(v6, v7); \
        v1 = ROT13(v1);   \
        v5 = ROT13(v5);   \
        v3 = ROT16(v3);   \
        v7 = ROT16(v7);   \
        v1 = XOR(v1, v0); \
        v5 = XOR(v5, v4); \
        v3 = XOR(v3, v2); \
        v7 = XOR(v7, v6); \
        v0 = ROT32(v0);   \
        v4 = ROT32(v4);   \
        v2 = ADD(v2, v1); \
        v6 = ADD(v6, v5); \
        v0 = ADD(v0, v3); \
        v4 = ADD(v4, v7); \
        v1 = ROT17(v1);   \
        v5 = ROT17(v5);   \
        v3 = ROT21(v3);   \
        v7 = ROT21(v7);   \
        v1 = XOR(v1, v2); \
        v5 = XOR(v5, v6); \
        v3 = XOR(v3, v0); \
        v7 = XOR(v7, v4); \
        v2 = ROT32(v2);   \
        v6 = ROT32(v6);   \
    } while (0)

#ifndef NSIPHASH
#define NSIPHASH 8
#endif

#endif

#ifndef NSIPHASH
#define NSIPHASH 1
#endif


#endif // ifdef INCLUDE_SIPHASHXN_H
