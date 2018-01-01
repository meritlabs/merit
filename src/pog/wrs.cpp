
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/wrs.h"

#include <cmath>
#include <limits>

namespace pog
{
    const double LOG_MAX_UINT64 = std::log(std::numeric_limits<uint64_t>::max());

    /* 
     * We need to compute a weighted key for each entrant in the lottery.
     * Int the RES algorithm by Efraimidis and Spirakis the weighted key is 
     * computed by rand^(1/W). Where rand is a uniform random value between 
     * [0,1] and W is a weight. 
     *
     * The weight in our case is the ANV of the address.
     *
     * Instead of computing the power above we will take the log as the weighted
     * key instead. log(rand^(1/W)) = log(rand) / W.
     */ 
    WeightedKey WeightedKeyForSampling(
            const uint256& rand_value,
            CAmount anv)
    {
        if(anv == 0) { 
            return -LOG_MAX_UINT64;
        }

        const auto rand_uint64 = SipHashUint256(0, 0, rand_value);

        if(rand_uint64 == 0) {
            return -LOG_MAX_UINT64;
        }

        /*
         * We can think of rand_uint64 as a random value between [0,1] if we take 
         * rand_uint64 and divide it the max uint64_t. 
         *
         * rand = rand_uint64/max_uint64_t
         *
         * log(rand) = log(rand_uint64/max_uint64_t) 
         *           = log(rand_uint64) - log(max_uint64_t)
         */
        const BigFloat log_rand = 
            std::log(rand_uint64) - LOG_MAX_UINT64;

        //We should get a negative number here.
        assert(log_rand <= 0);

        const BigFloat anv_f = anv;
        const WeightedKey weighted_key = log_rand / anv_f;

        return weighted_key;
    }

} //namespace pog
