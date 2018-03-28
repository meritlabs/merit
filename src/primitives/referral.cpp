// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Merit Foundation developers
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

#include <boost/algorithm/string.hpp>

namespace referral
{
    namespace
    {
        std::set<std::string> INVALID_ALIAS_NAMES = {
            "merit",
            "meritlabs",
        };

        /*
         * Older versions of GCC have a bug doing icase matching with character classes.
         * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71500
         */
        const std::regex ALIAS_REGEX(strprintf("^([a-z0-9A-Z_-]){3,%d}$", MAX_ALIAS_LENGTH), std::regex_constants::icase);

        /**
         * Only lowercase and exclude 0 and 1 to help reduce risk of homoglyph attacks
         * since '0' looks like 'o' and '1' looks like 'l'
         *
         * This leaves the name space to have a size of (34^2)*(36^28)
         */
        const std::regex SAFER_ALIAS_REGEX(strprintf("^[a-z0-9]([a-z0-9_-]){1,%d}[a-z0-9]$", SAFER_MAX_ALIAS_LENGTH));
    }

void NormalizeAlias(std::string& alias)
{
    boost::algorithm::trim(alias);
    if(alias.empty()) {
        return;
    }

    // Removes the @ symbol from an alias. Users may optionally put an @ symbol.
    // This function requires that the alias is not empty.
    if(alias[0] == '@') {
        alias.erase(0,1);
    }

    std::transform(alias.begin(), alias.end(), alias.begin(), ::tolower);
}

bool AliasesEqual(std::string a, std::string b, bool safe) {
    if (safe) {
        NormalizeAlias(a);
        NormalizeAlias(b);
    }

    return a == b;
}

bool CheckReferralAliasSafe(std::string alias) {
    NormalizeAlias(alias);

    if(alias.empty()) {
        return true;
    }

    if (!std::regex_match(alias, SAFER_ALIAS_REGEX)) {
        return false;
    }
    return INVALID_ALIAS_NAMES.count(alias) == 0;
}

bool CheckReferralAlias(std::string alias, bool normalize_alias)
{
    if(normalize_alias) {
        return CheckReferralAliasSafe(alias);
    }

    if(alias.empty()) {
        return true;
    }

    if (!std::regex_match(alias, ALIAS_REGEX)) {
        return false;
    }

    NormalizeAlias(alias);
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
        assert(aliasIn.size() <= MAX_ALIAS_LENGTH);
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
