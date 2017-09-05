// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

static const char DB_REFERRALS = 'r';

ReferralsViewDB::ReferralsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(GetDataDir() / "referrals", nCacheSize, fMemory, fWipe, true) {}

bool ReferralsViewDB::GetReferral(const uint256& code_hash, MutableReferral& referral) const {
    std::cerr << "Code Hash in GetReferral: " << code_hash.ToString() << std::endl;
    return m_db.Read(std::make_pair(DB_REFERRALS, code_hash), referral);
}

bool ReferralsViewDB::InsertReferral(const Referral& referral) {
    std::cerr << "Writing: " << referral.m_codeHash.ToString() << std::endl;
    return m_db.Write(std::make_pair(DB_REFERRALS, referral.m_codeHash), referral);
}

bool ReferralsViewDB::ReferralCodeExists(const uint256& code_hash) const {
    return m_db.Exists(std::make_pair(DB_REFERRALS, code_hash));
}

bool ReferralsViewDB::ListKeys() const {
    auto iter = m_db.NewIterator();
    iter->SeekToFirst();
    //std::cerr << "Is this thing valid?: " << (iter->Valid()) << std::endl;
    for (iter->Seek(0); iter->Valid(); iter->Next()) {
        if (!iter->value().empty()) {
            auto pair = std::make_pair(DB_REFERRALS, uint256{});
            bool got = iter->GetKey(pair);
            if(got)
            {
                std::cerr << "Key: " << pair.second.ToString() << std::endl;
                std::cerr << "Value: " << (iter->value().ToString()) << std::endl;
            }
        }
    }
    return true;
}

