// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG2_WRS_H
#define MERIT_POG2_WRS_H

#include "hash.h"
#include "amount.h"
#include <boost/multiprecision/cpp_dec_float.hpp> 

#include <map>

namespace pog2
{
    using BigFloat = boost::multiprecision::cpp_dec_float_50;
    using WeightedKey = BigFloat;

    WeightedKey WeightedKeyForSampling( const uint256& rand_value, CAmount anv);
} // namespace pog2

#endif //MERIT_POG2_WRS_H
