#ifndef FINITE_FIELD_H

#include "support/allocators/secure.h"

#include <vector>
#include <boost/multiprecision/cpp_int.hpp>

namespace finite_field
{
    const unsigned POWER_BITS = 521;
    const unsigned POWER_BYTES = POWER_BITS / 8;

    namespace bm = boost::multiprecision;
    using SecureCPPIntBackend = bm::cpp_int_backend<521, 521, bm::signed_magnitude, bm::unchecked, secure_allocator<bm::limb_type>>;
    using BigInt = bm::number<SecureCPPIntBackend>;

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

    BigInt PowerOf2(const unsigned&);
    BigInt ModPowerOf2(const BigInt&, const unsigned&);
    BigInt ModR(const BigInt&);
    BigInt AddModP(const BigInt&, const BigInt&);
    BigInt MultModP(const BigInt&, const BigInt&);
    BigInt DivModP(const BigInt&, const BigInt&);
    BigInt MinusModP(const BigInt&, const BigInt&);
    BigInt Negative(const BigInt&);
    BigInt ExpModP(const BigInt&, const BigInt&);
    BigInt InverseModP(const BigInt&);

    const BigInt R = PowerOf2(POWER_BITS);
    // mersenne prime
    const BigInt P = R - 1;

    Element EvalPolynomial(const Element&, const std::vector<Element>&);

} // namespace finite_field

#define FINITE_FIELD_H
#endif
