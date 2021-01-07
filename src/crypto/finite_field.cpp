// Copyright (c) 2017-2021 The Merit Foundation
#include "crypto/finite_field.h"

#include <numeric>

namespace finite_field
{
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

    BigInt PowerOf2(const unsigned& n)
    {
        BigInt one = 1;
        return one << n;
    }

    BigInt ModPowerOf2(const BigInt& x, const unsigned& n)
    {
        return x & (PowerOf2(n) - 1);
    }

    BigInt ModR(const BigInt& x)
    {
        return ModPowerOf2(x, POWER_BITS);
    }

    // montgomery modular multiplication
    // note: r = 1 (mod p) and p = -1 (mod r)
    //       so we can simplify this operation greatly
    BigInt redc(const BigInt& x)
    {
        auto m = ModR(x);
        auto t = (x + m * P) >> POWER_BITS;
        if(t < P) {
            return t;
        } else {
            return t - P;
        }
    }

    BigInt AddModP(const BigInt& left, const BigInt& right)
    {
        return redc(redc(left) + redc(right));
    }

    BigInt MultModP(const BigInt& left, const BigInt& right)
    {
        return redc(redc(left) * redc(right));
    }

    BigInt MinusModP(const BigInt& left, const BigInt& right)
    {
        return AddModP(left, -right);
    }

    BigInt Negative(const BigInt& x)
    {
        return MinusModP(0, x);
    }

    // exponentiation by squaring
    BigInt ExpBySquare(const BigInt& acc, const BigInt& base, const BigInt& exponent)
    {
        if(exponent < 0) {
            return ExpBySquare(acc, base, exponent + P - 1);
        } else if(exponent >= P) {
            return ExpBySquare(acc, base, (exponent - P) + 1);
        } else if(exponent == 0) {
            return acc;
        } else if(exponent == 1) {
            return MultModP(acc, base);
        } else if((exponent & 1) == 0) {
            return ExpBySquare(acc, MultModP(base, base), exponent >> 1);
        } else {
            return ExpBySquare(MultModP(acc, base), MultModP(base, base), exponent >> 1);
        }
    }

    BigInt ExpModP(const BigInt& base, const BigInt& exponent)
    {
        return ExpBySquare(1, base, exponent);
    }

    BigInt InverseModP(const BigInt& x)
    {
        return ExpModP(x, -1);
    }

    BigInt DivModP(const BigInt& numer, const BigInt& denom)
    {
        return MultModP(numer, InverseModP(denom));
    }

    // coefs are ordered such that the polynomial f(x) = sum (coefs[i] * x^i)
    Element EvalPolynomial(const Element& x, const std::vector<Element>& coefs)
    {
        return std::accumulate(coefs.crbegin(), coefs.crend(), Element{},
            [&x](const Element& acc, const Element& coef)
            {
                return (acc * x) + coef;
            });
    }

    Element::Element(BigInt val) : value{val} {}

    Element::Element() : value(0) {}

    bool Element::operator ==(const Element& right) const
    {
        return value == right.value;
    }

    Element Element::operator +(const Element& right) const
    {
        return AddModP(value, right.value);
    }

    Element Element::operator -(const Element& right) const
    {
        return MinusModP(value, right.value);
    }

    Element Element::operator *(const Element& right) const
    {
        return MultModP(value, right.value);
    }

    Element Element::operator /(const Element& right) const
    {
        return DivModP(value, right.value);
    }

} // namespace finite_field
