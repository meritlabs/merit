// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_REFDB_H
#define MERIT_REFDB_H

#include "dbwrapper.h"
#include "amount.h"
#include "primitives/referral.h"

#include <boost/optional.hpp>
#include <vector>

using MaybeReferral = boost::optional<Referral>;
using MaybeKeyID = boost::optional<CKeyID>;
using MaybeANV = boost::optional<CAmount>;
using ChildKeys = std::vector<CKeyID>;
using KeyIDs = std::vector<CKeyID>;

struct KeyANV
{
    CKeyID key;
    CAmount anv;
};

using KeyANVs = std::vector<KeyANV>;

class ReferralsViewDB
{
protected:
    mutable CDBWrapper m_db;
public:
    explicit ReferralsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    MaybeReferral GetReferral(const uint256&) const;
    MaybeKeyID GetReferrer(const CKeyID&) const;
    ChildKeys GetChildren(const CKeyID&) const;

    bool UpdateANV(const CKeyID&, CAmount);
    MaybeANV GetANV(const CKeyID&) const;
    KeyANVs GetAllANVs() const;

    bool InsertReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;
    bool WalletIdExists(const CKeyID&) const;
};

#endif
