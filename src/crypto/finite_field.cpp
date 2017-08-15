#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <iostream>
#include "finite_field.h"

namespace finite_field {

    // mersenne prime
    // const unsigned POWER(2281);
    const unsigned POWER(521);
    const BigInt R(PowerOf2(POWER));
    const BigInt P(R - 1);

    BigInt PowerOf2(const unsigned& n) {
        BigInt one(1);
        return one << n;
    }

    BigInt ModPowerOf2(const BigInt& x, const unsigned& n) {
        return x & (PowerOf2(n) - 1);
    }

    BigInt ModR(const BigInt& x) {
        return ModPowerOf2(x, POWER);
    }

    // montgomery modular multiplication
    // note: r = 1 (mod p) and p = -1 (mod r)
    //       so we can simplify this operation greatly
    BigInt redc(const BigInt& x) {
        auto m = ModR(x);
        auto t = (x + m * P) >> POWER;
        if(t < P) {
            return t;
        } else {
            return t - P;
        }
    }

    BigInt AddModP(const BigInt& left, const BigInt& right) {
        return redc(redc(left) + redc(right));
    }

    BigInt MultModP(const BigInt& left, const BigInt& right) {
        return redc(redc(left) * redc(right));
    }

    BigInt MinusModP(const BigInt& left, const BigInt& right) {
        return AddModP(left, -right);
    }

    BigInt Negative(const BigInt& x) {
        return MinusModP(0, x);
    }

    // exponentiation by squaring
    BigInt ExpBySquare(const BigInt& acc, const BigInt& base, const BigInt& exponent) {
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

    BigInt ExpModP(const BigInt& base, const BigInt& exponent) {
        return ExpBySquare(1, base, exponent);
    }

    BigInt InverseModP(const BigInt& x) {
        return ExpModP(x, -1);
    }

    BigInt DivModP(const BigInt& numer, const BigInt& denom) {
        return MultModP(numer, InverseModP(denom));
    }

    // coefs are ordered such that the polynomial f(x) = sum (coefs[i] * x^i)
    BigInt EvalPolynomial(const BigInt& x, const std::vector<BigInt>& coefs) {
        BigInt res(0);
        for(auto it = coefs.crbegin(); it < coefs.crend(); it++){
            res = MultModP(res, x) + *it;
        }
        return res;
    }

}
