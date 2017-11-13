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

namespace referral
{

static inline std::string GenerateReferralCode()
{
    auto randomHash = GetRandHash();

    return randomHash.ToString().substr(0, 10);
}

MutableReferral::MutableReferral(
        char addressTypeIn,
        const Address& addressIn,
        const uint256& referralIn) :
    version{Referral::CURRENT_VERSION},
    previousReferral{referralIn},
    addressType{addressTypeIn},
    pubKeyId{addressIn},
    code{GenerateReferralCode()},
    codeHash{Hash(code.begin(), code.end())} {}

MutableReferral::MutableReferral(const Referral& ref) :
    version{ref.version},
    previousReferral{ref.previousReferral},
    addressType{ref.addressType},
    pubKeyId{ref.pubKeyId},
    code{ref.code},
    codeHash{ref.codeHash} {}

uint256 MutableReferral::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

uint256 Referral::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
Referral::Referral(
        char addressTypeIn,
        const Address& addressIn,
        const uint256& referralIn) :
    version{Referral::CURRENT_VERSION},
    previousReferral{referralIn},
    addressType{addressTypeIn},
    pubKeyId{addressIn},
    code{GenerateReferralCode()},
    codeHash{Hash(code.begin(), code.end())},
    m_hash{} {}

Referral::Referral(const MutableReferral &ref) :
    version{ref.version},
    previousReferral{ref.previousReferral},
    addressType{ref.addressType},
    pubKeyId{ref.pubKeyId},
    code{ref.code},
    codeHash{ref.codeHash},
    m_hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    version{ref.version},
    previousReferral{std::move(ref.previousReferral)},
    addressType{ref.addressType},
    pubKeyId{std::move(ref.pubKeyId)},
    code{ref.code},
    codeHash{ref.codeHash},
    m_hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, codeHash=%s, previousReferral=%s, addressType%d, pubKeyId=%s)\n",
        GetHash().GetHex().substr(0,10),
        version,
        codeHash.GetHex(),
        previousReferral.GetHex(),
        static_cast<int>(addressType),
        pubKeyId.GetHex());
    return str;
}

}
