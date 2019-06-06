// Copyright (c) 2017-2019 The Merit Foundation
#include "crypto/finite_field.h"
#include "crypto/legendre.h"
#include "crypto/shamir.h"
#include "key.h"
#include "random.h"
#include <algorithm>
#include <vector>
#include <boost/multiprecision/cpp_int.hpp>

namespace shamir
{
    namespace ff = finite_field;
    namespace bm = boost::multiprecision;

    // TODO: figure out good values or make it configurable
    namespace
    {
        const int K = 5;
        const int N = 10;
        const int KEY_SIZE = 32;
    } // namespace

    ff::BigInt KeyToBigInt(const CPrivKey& key)
    {
        ff::BigInt val;
        for(const auto& ch : key) {
            val <<= 8;
            val += ch;
        }
        return val;
    }

    ff::Element RandomElement()
    {
        CPrivKey buffer(ff::POWER_BYTES);
        GetStrongRandBytes(buffer.data(), ff::POWER_BYTES);
        return KeyToBigInt(buffer);
    }

    CPrivKey BigIntToKey(ff::BigInt val)
    {
        CPrivKey key(KEY_SIZE);
        for(auto it = key.rbegin(); it != key.rend(); ++it) {
            bm::cpp_int bits = val & 0xff; // intermediate cast
            *it = static_cast<unsigned char>(bits);
            val >>= 8;
        }
        return key;
    }

    std::vector<legendre::FieldPoint> ShardKey(const CPrivKey& key)
    {
        ff::Element secret = KeyToBigInt(key);

        // get k-1 random finite_field::Elements to use as coefficients
        std::vector<ff::Element> coefs(K);
        coefs[0] = secret;
        std::generate_n(coefs.begin() + 1, K - 1, RandomElement);

        // get n random finite_field::Elements to use as inputs for shards
        std::vector<ff::Element> inputs(N);
        std::generate_n(inputs.begin(), N, RandomElement);

        std::vector<legendre::FieldPoint> points(N);
        std::transform(inputs.begin(), inputs.end(), points.begin(),
            [&coefs](const ff::Element input)
            {
                return legendre::FieldPoint{input, ff::EvalPolynomial(input, coefs)};
            });
        return points;
    }

} // namespace shamir
