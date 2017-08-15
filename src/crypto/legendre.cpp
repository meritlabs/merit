#include "finite_field.h"
#include "field_point.h"
#include "legendre.h"
#include <vector>

namespace legendre
{
using namespace finite_field;

BigInt LegendrePolyAtZero(const std::vector<FieldPoint>& points) {
    BigInt sum(0);
    for(auto point : points) {
        sum = AddModP(sum, DivModP(NumerTerm(point, points),
                                       DenomTerm(point, points)));
    }
    return sum;
}

BigInt NumerTerm(const FieldPoint& term, const std::vector<FieldPoint>& points) {
    BigInt prod(term.y_value);
    for(auto point : points){
        if(point.x_value != term.x_value) {
            prod = MultModP(prod, point.x_value);
        }
    }
    return prod;
}

BigInt DenomTerm(const FieldPoint& term, const std::vector<FieldPoint>& points) {
    BigInt prod(1);
    for(auto point : points){
        if(point.x_value != term.x_value) {
            prod = MultModP(prod, (MinusModP(point.x_value, term.x_value)));
        }
    }
    return prod;
}
}
