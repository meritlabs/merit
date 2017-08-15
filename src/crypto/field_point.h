#ifndef FIELD_POINT_H
#include "finite_field.h"

struct FieldPoint
{
    BigInt x_value;
    BigInt y_value;
};

#define FIELD_POINT_H
#endif