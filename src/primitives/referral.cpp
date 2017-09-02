// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/referral.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "random.h"

#include <utility>

static inline std::string GenerateReferralCode()
{
    auto randomHash = GetRandHash();

    return randomHash.ToString().substr(0, 10);
}

MutableReferral::MutableReferral(CKeyID& addressIn, uint256 referralIn) :
    m_nVersion{Referral::CURRENT_VERSION},
    m_previousReferral{referralIn},
    m_pubKeyId{addressIn},
    m_code{GenerateReferralCode()},
    m_codeHash{uint256S(m_code)} {}

MutableReferral::MutableReferral(const Referral& ref) :
    m_nVersion{ref.m_nVersion},
    m_previousReferral{ref.m_previousReferral},
    m_pubKeyId{ref.m_pubKeyId},
    m_code{ref.m_code},
    m_codeHash{ref.m_codeHash} {}

uint256 MutableReferral::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

uint256 Referral::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
Referral::Referral(CKeyID& addressIn, uint256 referralIn) :
    m_nVersion{Referral::CURRENT_VERSION},
    m_previousReferral{referralIn},
    m_pubKeyId{addressIn},
    m_code{GenerateReferralCode()},
    m_codeHash{uint256S(m_code)},
    m_hash{} {}

Referral::Referral(const MutableReferral &ref) :
    m_nVersion{ref.m_nVersion},
    m_previousReferral{ref.m_previousReferral},
    m_pubKeyId{ref.m_pubKeyId},
    m_code{ref.m_code},
    m_codeHash{ref.m_codeHash},
    m_hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    m_nVersion{ref.m_nVersion},
    m_previousReferral{std::move(ref.m_previousReferral)},
    m_pubKeyId{std::move(ref.m_pubKeyId)},
    m_code{ref.m_code},
    m_codeHash{ref.m_codeHash},
    m_hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, m_previousReferral=%s, m_pubKeyId=%s)\n",
        GetHash().ToString().substr(0,10),
        m_nVersion,
        HexStr(m_previousReferral),
        HexStr(m_pubKeyId));
    return str;
}

ReferralsViewBacked::ReferralsViewBacked(ReferralsView *viewIn) { base = viewIn; }

bool ReferralsViewBacked::GetReferral(const uint256& code, MutableReferral& m_ref) const { return base->GetReferral(code, m_ref); }
bool ReferralsViewBacked::InsertReferral(const Referral& ref) { return base->InsertReferral(ref); }
void ReferralsViewBacked::SetBackend(ReferralsView& view_in) { base = &view_in; }
bool ReferralsViewBacked::ReferralCodeExists(const uint256& code) const {return base->ReferralCodeExists(code); }

ReferralMap::iterator ReferralsViewCache::Fetch(const uint256& code) const {
    return referral_cache.find(code);
}

bool ReferralsViewCache::GetReferral(const uint256& code, MutableReferral& m_ref) const {
    auto it = Fetch(code);
    if (it != referral_cache.end()) {
        m_ref = it->second;
        return true;
    }
    return false;
}

bool ReferralsViewCache::InsertReferral(const Referral& ref) {
    auto ret = referral_cache.insert(std::make_pair(ref.m_codeHash, ref));
    return ret.second;
}

bool ReferralsViewCache::ReferralCodeExists(const uint256& code) const {
    return !(Fetch(code) == referral_cache.end());
}

void ReferralsViewCache::Flush() {
    for (auto pr : referral_cache) {
        base->InsertReferral(pr.second);
    }
    referral_cache.clear();
}
