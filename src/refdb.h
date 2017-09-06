// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_REFDB_H
#define BITCOIN_REFDB_H

#include "dbwrapper.h"
#include "primitives/referral.h"

#include <boost/optional.hpp>

using MaybeReferral = boost::optional<Referral>;
using MaybeKeyID = boost::optional<CKeyID>;
using ChildKeys = std::vector<CKeyID>;

class ReferralsViewDB
{
protected:
    CDBWrapper m_db;
public:
    explicit ReferralsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    MaybeReferral GetReferral(const uint256&) const;
    MaybeKeyID GetRefferer(const CKeyID&) const;
    ChildKeys GetChildren(const CKeyID&) const;

    bool InsertReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;
    bool WalletIdExists(const CKeyID&) const;
};

#endif
