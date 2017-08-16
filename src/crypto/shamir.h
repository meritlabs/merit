#ifndef SHAMIR_H

#include "crypto/finite_field.h"
#include "crypto/field_point.h"
#include "crypto/legendre.h"
#include "key.h"
#include <algorithm>
#include <vector>

namespace shamir
{
    namespace ff = finite_field;

    ff::BigInt KeyToBigInt(const CPrivKey&);
    CPrivKey BigIntToKey(ff::BigInt);
    std::vector<ff::FieldPoint> ShardKey(const CPrivKey&);
    ff::Element RandomElement();

} // namespace shamir

#define SHAMIR_H
#endif
