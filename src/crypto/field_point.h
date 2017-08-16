#ifndef FIELD_POINT_H
#include "crypto/finite_field.h"

namespace finite_field
{
    struct FieldPoint
    {
        Element m_x_element;
        Element m_y_element;
    };
} // namespace finite_field

#define FIELD_POINT_H
#endif
