#ifndef LEGENDRE_H

#include "finite_field.h"
#include "field_point.h"
#include <vector>

namespace legendre {
  finite_field::BigInt legendre_poly_at_zero(const std::vector<FieldPoint>&);
  finite_field::BigInt numer_term(const FieldPoint&, const std::vector<FieldPoint>&);
  finite_field::BigInt denom_term(const FieldPoint&, const std::vector<FieldPoint>&);
}

#define LEGENDRE_H
#endif
