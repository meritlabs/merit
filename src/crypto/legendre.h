#ifndef LEGENDRE_H

#include "field_point.h"

#include <vector>

namespace legendre {
  finite_field::Element LegendrePolyAtZero(const std::vector<FieldPoint>&);
  finite_field::Element NumerTerm(const FieldPoint&, const std::vector<FieldPoint>&);
  finite_field::Element DenomTerm(const FieldPoint&, const std::vector<FieldPoint>&);
}

#define LEGENDRE_H
#endif
