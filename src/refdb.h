// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_REFDB_H
#define BITCOIN_REFDB_H

#include "dbwrapper.h"
#include "primitives/referral.h"

class ReferralsViewDB
{
protected:
    mutable CDBWrapper m_db;
public:
    explicit ReferralsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetReferral(const uint256&, MutableReferral&) const;
    bool InsertReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;
    bool ListKeys() const;
};

#endif
