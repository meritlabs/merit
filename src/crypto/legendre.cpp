// Copyright (c) 2017-2021 The Merit Foundation
#include "legendre.h"
#include <numeric>
#include <vector>

namespace legendre
{
namespace ff = finite_field;

ff::Element LegendrePolyAtZero(const std::vector<FieldPoint>& points)
{
    return std::accumulate(points.begin(), points.end(), ff::Element{},
        [&points](const ff::Element& acc, const FieldPoint& point) {
            return acc + (NumerTerm(point, points) / DenomTerm(point, points));
        });
}

ff::Element NumerTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    return std::accumulate(points.begin(), points.end(), term.m_y_element,
        [&term, &points](const ff::Element& acc, const legendre::FieldPoint& point) {
            return point.m_x_element == term.m_x_element
                ? acc
                : acc * point.m_x_element;
            });
}

ff::Element DenomTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    return std::accumulate(points.begin(), points.end(), ff::Element{1},
        [&term, &points](const ff::Element& acc, const legendre::FieldPoint& point) {
            return point.m_x_element == term.m_x_element
                ? acc
                : acc * (point.m_x_element - term.m_x_element);
            });
}
} // namespace legendre
