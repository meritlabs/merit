// Copyright (c) 2017-2019 The Merit Foundation
#ifndef FINITE_FIELD_H

#include "support/allocators/secure.h"

#include <vector>
#include <boost/multiprecision/cpp_int.hpp>

namespace finite_field
{
    const unsigned POWER_BITS = 521;
    const unsigned POWER_BYTES = POWER_BITS / 8;

    namespace bm = boost::multiprecision;
    using SecureCPPIntBackend = bm::cpp_int_backend<POWER_BITS, POWER_BITS, bm::signed_magnitude, bm::unchecked, secure_allocator<bm::limb_type>>;
    using BigInt = bm::number<SecureCPPIntBackend>;

    extern const BigInt R;
    extern const BigInt P;

    class Element
    {
    public:
        BigInt value;
        Element(BigInt);
        Element();
        bool operator ==(const Element& right) const;
        Element operator +(const Element& right) const;
        Element operator -(const Element& right) const;
        Element operator *(const Element& right) const;
        Element operator /(const Element& right) const;
    };

    Element EvalPolynomial(const Element&, const std::vector<Element>&);

} // namespace finite_field

#define FINITE_FIELD_H
#endif
