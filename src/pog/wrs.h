// Copyright (c) 2017-2020 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_WRS_H
#define MERIT_POG_WRS_H

#include "hash.h"
#include "amount.h"
#include <boost/multiprecision/cpp_dec_float.hpp> 

#include <map>

namespace pog 
{
    using BigFloat = boost::multiprecision::cpp_dec_float_50;
    using WeightedKey = BigFloat;

    WeightedKey WeightedKeyForSampling( const uint256& rand_value, CAmount anv);
} // namespace pog

#endif //MERIT_POG_WRS_H
