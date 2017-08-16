#ifndef FIELD_POINT_H
#include "finite_field.h"

struct FieldPoint
{
    finite_field::Element m_x_element;
    finite_field::Element m_y_element;
};

#define FIELD_POINT_H
#endif