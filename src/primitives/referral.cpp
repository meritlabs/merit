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

ReferralCode::ReferralCode() {
    m_code = GetRandHash().ToString().substr(0, 10);
    m_hash = uint256S(m_code);
}

ReferralCode::ReferralCode(std::string& code): m_code(code)
{
    m_hash = uint256S(code);
}

std::string ReferralCode::GetCode() const
{
    return m_code;
}

uint256 ReferralCode::GetHash() const
{
    return m_hash;
}

std::string ReferralCode::ToString() const
{
    return strprintf("ReferralCode(code=%s, hash=%s\n", m_code, m_hash.ToString());
}

MutableReferral::MutableReferral() :
    nVersion{Referral::CURRENT_VERSION},
    previousReferral{},
    scriptSig{},
    code{ReferralCode()} {}

MutableReferral::MutableReferral(const Referral& ref) :
    nVersion{ref.nVersion},
    previousReferral{ref.previousReferral},
    scriptSig{ref.scriptSig},
    code{ref.code} {}

uint256 MutableReferral::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

uint256 Referral::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
Referral::Referral() :
    nVersion{Referral::CURRENT_VERSION},
    previousReferral{},
    scriptSig{},
    code{ReferralCode()},
    hash{} {}

Referral::Referral(const ReferralCode codeIn) :
    nVersion{Referral::CURRENT_VERSION},
    previousReferral{},
    scriptSig{},
    code{codeIn},
    hash{} {}

Referral::Referral(const MutableReferral &ref) :
    nVersion{ref.nVersion},
    previousReferral{ref.previousReferral},
    scriptSig{ref.scriptSig},
    code{ref.code},
    hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    nVersion{ref.nVersion},
    previousReferral{std::move(ref.previousReferral)},
    scriptSig{std::move(ref.scriptSig)},
    code{ref.code},
    hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, previousReferral=%s, scriptSig=%s)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        HexStr(previousReferral),
        HexStr(scriptSig));
    return str;
}
