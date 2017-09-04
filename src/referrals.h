// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REFERRALS_H
#define REFERRALS_H

#include "primitives/referral.h"
#include "refdb.h"
#include "sync.h"

#include <unordered_map>

typedef std::unordered_map<uint256, Referral> ReferralMap;

class ReferralsView
{
private:
    ReferralMap::iterator Fetch(const uint256& code) const;
    mutable CCriticalSection m_cs_cache;
protected:
    ReferralsViewDB *m_db;
    mutable ReferralMap m_referral_cache;
public:
    ReferralsView(ReferralsViewDB*);

    bool GetReferral(const uint256&, MutableReferral&) const;
    bool InsertReferral(const Referral&);
    bool ReferralCodeExists(const uint256&) const;

    void Flush();
};

#endif
