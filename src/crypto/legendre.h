// Copyright (c) 2017-2019 The Merit Foundation developers
#ifndef LEGENDRE_H

#include "finite_field.h"

#include <vector>

namespace legendre
{
    namespace ff = finite_field;
    struct FieldPoint
    {
        ff::Element m_x_element;
        ff::Element m_y_element;
    };

    ff::Element LegendrePolyAtZero(const std::vector<FieldPoint>&);
    ff::Element NumerTerm(const FieldPoint&, const std::vector<FieldPoint>&);
    ff::Element DenomTerm(const FieldPoint&, const std::vector<FieldPoint>&);
} // namespace legendre

#define LEGENDRE_H
#endif
