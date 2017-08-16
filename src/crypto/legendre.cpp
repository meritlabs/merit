#include "legendre.h"
#include <vector>

namespace legendre
{
using namespace finite_field;

Element LegendrePolyAtZero(const std::vector<FieldPoint>& points)
{
    Element sum(0);
    for(auto point : points) {
        sum = sum + (NumerTerm(point, points) / DenomTerm(point, points));
    }
    return sum;
}

Element NumerTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    Element prod(term.m_y_element.value);
    for(auto point : points) {
        if(point.m_x_element.value != term.m_x_element.value) {
            prod = prod * point.m_x_element;
        }
    }
    return prod;
}

Element DenomTerm(const FieldPoint& term, const std::vector<FieldPoint>& points)
{
    Element prod(1);
    for(auto point : points) {
        if(point.m_x_element.value != term.m_x_element.value) {
            prod = prod * (point.m_x_element - term.m_x_element);
        }
    }
    return prod;
}
}
