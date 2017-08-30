#include "crypto/shamir.h"
#include "crypto/finite_field.h"
#include "crypto/legendre.h"
#include "key.h"
#include "random.h"
#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <utility>
#include <vector>

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
    for (const auto& ch : key) {
        val <<= 8;
        val += ch;
    }
    return val;
}

ff::Element RandomElement()
{
    CPrivKey buffer(KEY_SIZE);
    GetStrongRandBytes(buffer.data(), KEY_SIZE);
    return KeyToBigInt(buffer);
}

CPrivKey BigIntToKey(ff::BigInt val)
{
    CPrivKey key(KEY_SIZE);
    for (auto it = key.rbegin(); it != key.rend(); ++it) {
        bm::cpp_int bits = val & 0xff; // intermediate cast
        *it = static_cast<unsigned char>(bits);
        val >>= 8;
    }
    return key;
}

std::vector<legendre::FieldPoint> ShardElement(const ff::Element& secret)
{
    // get k-1 random finite_field::Elements to use as coefficients
    std::vector<ff::Element> coefs(K);
    coefs[0] = secret;
    std::generate_n(coefs.begin() + 1, K - 1, RandomElement);

    // get n random finite_field::Elements to use as inputs for shards
    std::vector<ff::Element> inputs(N);
    std::generate_n(inputs.begin(), N, RandomElement);

    std::vector<legendre::FieldPoint> points(N);
    std::transform(inputs.begin(), inputs.end(), points.begin(),
        [&coefs](const ff::Element input) {
            return legendre::FieldPoint{input, ff::EvalPolynomial(input, coefs)};
        });
    return points;
}

std::vector<std::pair<CPrivKey, CPrivKey>> ShardKey(const CPrivKey& key)
{
    ff::Element secret = KeyToBigInt(key);
    auto points = ShardElement(secret);
    std::vector<std::pair<CPrivKey, CPrivKey>> shards(N);
    std::transform(points.begin(), points.end(), shards.begin(),
        [](const legendre::FieldPoint& point) {
            return std::make_pair(
                BigIntToKey(point.m_x_element.value),
                BigIntToKey(point.m_y_element.value));
        });
    return shards;
}

CPrivKey RecoverKey(const std::vector<std::pair<CPrivKey, CPrivKey>>& shards)
{
    assert(shards.size() >= K);
    std::vector<legendre::FieldPoint> points(shards.size());
    std::transform(shards.begin(), shards.end(), points.begin(),
        [](const std::pair<CPrivKey, CPrivKey>& shard) {
            return legendre::FieldPoint{
                KeyToBigInt(shard.first),
                KeyToBigInt(shard.second)};
        });

    auto secret = legendre::LegendrePolyAtZero(points).value;
    return BigIntToKey(secret);
}
} // namespace shamir
