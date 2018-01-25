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

#include <set>
#include <cctype>
#include <algorithm>
#include <regex>

namespace referral
{
    namespace
    {
        std::set<std::string> INVALID_ALIAS_NAMES = {
            "merit",
            "meritlabs",
        };

    }

bool CheckReferralAlias(std::string alias)
{
    std::regex alias_regex(strprintf("^([a-z0-9_-]){0,%d}$", MAX_ALIAS_LENGTH), std::regex_constants::icase);

    // check alias contains only valid symbols
    if (!std::regex_match(alias, alias_regex)) {
        return false;
    }

    // check alias is not one of the reserved names
    std::transform(alias.begin(), alias.end(), alias.begin(), ::tolower);
    return INVALID_ALIAS_NAMES.count(alias) == 0;
}

MutableReferral::MutableReferral(
        char addressTypeIn,
        const Address& addressIn,
        const CPubKey& pubkeyIn,
        const Address& parentAddressIn,
        std::string aliasIn,
        int32_t versionIn) :
    version{versionIn},
    parentAddress{parentAddressIn},
    addressType{addressTypeIn},
    pubkey{pubkeyIn},
    signature{valtype()}
    {
        if (addressType == 1) {
            address = addressIn;
        } else {
            uint160 pubkeyHash = pubkey.GetID();
            MixAddresses(addressIn, pubkeyHash, address);
        }

        if (version >= Referral::INVITE_VERSION) {
            alias = aliasIn;
        }
    }


MutableReferral::MutableReferral(const Referral& ref) :
    address{ref.address},
    version{ref.version},
    parentAddress{ref.parentAddress},
    addressType{ref.addressType},
    pubkey{ref.pubkey},
    signature{ref.signature},
    alias{ref.alias} {}

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
    alias{ref.alias},
    address{ref.address},
    hash{ComputeHash()} {}

Referral::Referral(MutableReferral &&ref) :
    version{ref.version},
    parentAddress{std::move(ref.parentAddress)},
    addressType{ref.addressType},
    pubkey{std::move(ref.pubkey)},
    signature{std::move(ref.signature)},
    alias{std::move(ref.alias)},
    address{std::move(ref.address)},
    hash{ComputeHash()} {}

unsigned int Referral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string Referral::ToString() const
{
    std::string str;
    str += strprintf("Referral(hash=%s, ver=%d, addressType=%d, alias=%s)\n",
        GetHash().GetHex(),
        version,
        static_cast<int>(addressType),
        alias);
    return str;
}

}
