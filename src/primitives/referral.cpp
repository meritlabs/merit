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
        const Address& parentAddressIn,
        std::string tagIn,
        int32_t versionIn) :
    version{versionIn},
    parentAddress{parentAddressIn},
    addressType{addressTypeIn},
    pubkey{pubkeyIn},
    signature{valtype()},
    tag{tagIn}
    {
        assert(tagIn.size() <= MAX_TAG_LENGTH);

        if (addressType == 1) {
            address = addressIn;
        } else {
            uint160 pubkeyHash = pubkey.GetID();
            MixAddresses(addressIn, pubkeyHash, address);
        }
    }


MutableReferral::MutableReferral(const Referral& ref) :
    address{ref.address},
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    pubkey{ref.pubkey},
    signature{ref.signature},
    tag{ref.tag} {}

Address MutableReferral::GetAddress() const
{
    return address;
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
    tag{ref.tag},
    address{ref.address},
    hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    version{ref.version},
    parentAddress{std::move(ref.parentAddress)},
    addressType{ref.addressType},
    pubkey{std::move(ref.pubkey)},
    signature{std::move(ref.signature)},
    tag{std::move(ref.tag)},
    address{std::move(ref.address)},
    hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, addressType=%d, tag=%s)\n",
        GetHash().GetHex(),
        version,
        static_cast<int>(addressType),
        tag);
    return str;
}

}
