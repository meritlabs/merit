#include "legendre.h"
#include <vector>

namespace legendre
{
namespace ff = finite_field;

ff::Element LegendrePolyAtZero(const std::vector<FieldPoint>& points)
{
    ff::Element sum(0);
    for(const auto& point : points) {
        sum = sum + (NumerTerm(point, points) / DenomTerm(point, points));
    }
    return sum;
}

ff::Element NumerTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    ff::Element prod(term.m_y_element.value);
    for(const auto& point : points) {
        if(point.m_x_element.value != term.m_x_element.value) {
            prod = prod * point.m_x_element;
        }
    }
    return prod;
}

ff::Element DenomTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    ff::Element prod(1);
    for(const auto& point : points) {
        if(point.m_x_element.value != term.m_x_element.value) {
            prod = prod * (point.m_x_element - term.m_x_element);
        }
    }
    return prod;
}
} // namespace legendre
