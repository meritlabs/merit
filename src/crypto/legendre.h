#ifndef LEGENDRE_H

#include "finite_field.h"
#include "field_point.h"

#include <vector>

namespace legendre {
    namespace ff = finite_field;
    ff::Element LegendrePolyAtZero(const std::vector<ff::FieldPoint>&);
    ff::Element NumerTerm(const ff::FieldPoint&, const std::vector<ff::FieldPoint>&);
    ff::Element DenomTerm(const ff::FieldPoint&, const std::vector<ff::FieldPoint>&);
} // namespace legendre

#define LEGENDRE_H
#endif
