#ifndef FINITE_FIELD_H

#include "support/allocators/secure.h"

#include <vector>
#include <boost/multiprecision/cpp_int.hpp>

namespace finite_field
{
    namespace bm = boost::multiprecision;
    using SecureCPPIntBackend = bm::cpp_int_backend<0, 0, bm::signed_magnitude, bm::unchecked, secure_allocator<bm::limb_type>>;
    using BigInt = bm::number<SecureCPPIntBackend>;
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
    BigInt EvalPolynomial(const BigInt&, const std::vector<BigInt>&);

    class Element
    {
    public:
        BigInt value;
        Element(BigInt val) : value{val}
        {
        }

        Element() : value(0)
        {
        }
        friend bool operator ==(const Element& left, const Element& right)
        {
            return left.value == right.value;
        }
        friend Element operator +(const Element& left, const Element& right)
        {
            return Element(AddModP(left.value, right.value));
        }
        friend Element operator -(const Element& left, const Element& right)
        {
            return Element(MinusModP(left.value, right.value));
        }
        friend Element operator *(const Element& left, const Element& right)
        {
            return Element(MultModP(left.value, right.value));
        }
        friend Element operator /(const Element& left, const Element& right)
        {
            return Element(DivModP(left.value, right.value));
        }
    };

}

#define FINITE_FIELD_H
#endif
