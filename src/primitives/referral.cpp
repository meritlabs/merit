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

MutableReferral::MutableReferral(
        char addressTypeIn,
        const Address& addressIn,
        const MaybePubKey& pubkeyIn,
        const Address& parentAddressIn) :
    version{Referral::CURRENT_VERSION},
    parentAddress{parentAddressIn},
    addressType{addressTypeIn},
    address{addressIn},
    pubkey{pubkeyIn},
    signature{valtype()} {}

MutableReferral::MutableReferral(const Referral& ref) :
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    address{ref.address},
    pubkey{ref.pubkey},
    signature{ref.signature} {}

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
        const MaybePubKey& pubkeyIn,
        const Address& parentAddressIn) :
    version{Referral::CURRENT_VERSION},
    parentAddress{parentAddressIn},
    addressType{addressTypeIn},
    address{addressIn},
    pubkey{pubkeyIn},
    signature{valtype()},
    hash{} {}

Referral::Referral(const MutableReferral &ref) :
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    address{ref.address},
    pubkey{ref.pubkey},
    signature{ref.signature},
    hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    version{ref.version},
    parentAddress{std::move(ref.parentAddress)},
    addressType{ref.addressType},
    address{std::move(ref.address)},
    pubkey{std::move(ref.pubkey)},
    signature{ref.signature},
    hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, parentAddress=%s, address=%s, addressType=%d)\n",
        GetHash().GetHex(),
        version,
        parentAddress.GetHex(),
        address.GetHex(),
        static_cast<int>(addressType));
    return str;
}

}
