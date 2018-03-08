// Copyright (c) 2017-2018 The Merit Foundation developers
#ifndef SHAMIR_H

#include "crypto/finite_field.h"
#include "crypto/legendre.h"
#include "key.h"
#include <algorithm>
#include <vector>

namespace shamir
{
    std::vector<legendre::FieldPoint> ShardKey(const CPrivKey&);
} // namespace shamir

#define SHAMIR_H
#endif
