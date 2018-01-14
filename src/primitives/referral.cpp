// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/referral.h"
#include "script/standard.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "random.h"

namespace referral
{

MutableReferral::MutableReferral(
        char addressTypeIn,
        const Address& addressIn,
        const CPubKey& pubkeyIn,
        const Address& parentAddressIn) :
    address{addressIn},
    version{Referral::CURRENT_VERSION},
    parentAddress{parentAddressIn},
    addressType{addressTypeIn},
    pubkey{pubkeyIn},
    signature{valtype()}
    {}


MutableReferral::MutableReferral(const Referral& ref) :
    address{ref.address},
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    pubkey{ref.pubkey},
    signature{ref.signature} {}

Address MutableReferral::GetAddress() const
{
    Address computed_address;
    if (addressType == 1) {
        computed_address = address;
    } else {
        uint160 pubkeyHash = pubkey.GetID();
        MixAddresses(address, pubkeyHash, computed_address);
    }
    return computed_address;
}

uint256 MutableReferral::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

uint256 Referral::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

Referral::Referral(const MutableReferral &ref) :
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    pubkey{ref.pubkey},
    signature{ref.signature},
    address{ref.address},
    hash{ComputeHash()},
    computed_address{ref.GetAddress()} {}

Referral::Referral(MutableReferral &&ref) :
    version{ref.version},
    parentAddress{std::move(ref.parentAddress)},
    addressType{ref.addressType},
    pubkey{std::move(ref.pubkey)},
    signature{std::move(ref.signature)},
    address{std::move(ref.address)},
    computed_address{ref.GetAddress()},
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
