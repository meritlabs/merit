// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_POG_ANV_H
#define MERIT_POG_ANV_H

#include "primitives/referral.h"
#include "refdb.h"

namespace pog 
{
    uint64_t ComputeANV(const CKeyID&, const ReferralsViewDB&);

} // namespace pog

#endif //MERIT_POG_ANV_H
