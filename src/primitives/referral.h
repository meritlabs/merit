// Copyright (c) 2017-2021 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_PRIMITIVES_REFERRAL_H
#define MERIT_PRIMITIVES_REFERRAL_H

#include "consensus/params.h"
#include "hash.h"
#include "pubkey.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"

#include <stdint.h>
#include <vector>
#include <boost/optional.hpp>
#include <boost/variant.hpp>

typedef std::vector<unsigned char> valtype;

namespace referral
{
using Address = uint160;
using ReferralId = boost::variant<uint256, referral::Address, std::string>;

struct MutableReferral;

static const int MAX_ALIAS_LENGTH = 20;

//This is 2 less than MAX_ALIAS_LENGTH because the SAFER_ALIAS_REGEX validates
//the other 2 characters. So the ultimate length stays the same.
static const int SAFER_MAX_ALIAS_LENGTH = 18;

struct MutableReferral;

/** The basic referral that is broadcast on the network and contained in
 * blocks. A referral references a previous referral which helps construct the
 * referral tree.
 */
class Referral
{
friend struct MutableReferral;

public:
    // Default referral version.
    static const int32_t CURRENT_VERSION = 0;
    static const int32_t INVITE_VERSION = 1;

    // Changing the default referral version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION = 1;

    const int32_t version;

    // address of previous referral
    Address parentAddress;

    // Type of address. 1 == Key ID, 2 = Script ID, 3 = Parameterized Script ID
    const char addressType;

    // pubky used to sign referral
    // pubkey of beaconed address in case addressType = 1
    // signer pubkey otherwise
    const CPubKey pubkey;

    // signature of parentAddress + address
    const valtype signature;

    // referral alias aka name
    const std::string alias;

private:
    const Address address;

    /** Memory only. */
    const uint256 hash;

    uint256 ComputeHash() const;

public:
    /** Convert a MutableReferral into a Referral. */
    Referral(const MutableReferral& ref);
    Referral(MutableReferral&& ref);

    template <typename Stream>
    inline void Serialize(Stream& s) const
    {
        SerializeReferral(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    Referral(deserialize_type, Stream& s) : Referral(MutableReferral(deserialize, s)) { }

    const uint256& GetHash() const
    {
        return hash;
    }

    const Address& GetAddress() const
    {
        return address;
    }

    std::string GetAlias() const;

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    friend bool operator==(const Referral& a, const Referral& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const Referral& a, const Referral& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;

    template <typename Stream, typename RefType>
    friend inline void SerializeReferral(const RefType& ref, Stream& s);

    template <typename Stream, typename RefType>
    friend void UnserializeReferral(RefType& ref, Stream& s);
};

/** A mutable version of Referral. */
struct MutableReferral {
friend class Referral;

    Address address;

public:
    int32_t version;
    Address parentAddress;
    char addressType;
    CPubKey pubkey;
    valtype signature;
    std::string alias;

    MutableReferral(int32_t versionIn = Referral::CURRENT_VERSION) : version(versionIn),
                                                                     addressType{0},
                                                                     alias{""} {}

    MutableReferral(const Referral& ref);

    MutableReferral(
        char addressTypeIn,
        const Address& addressIn,
        const CPubKey& pubkeyIn,
        const Address& parentAddressIn,
        std::string aliasIn = "",
        int32_t versionIn = Referral::CURRENT_VERSION);

    Address GetAddress() const;

    template <typename Stream>
    inline void Serialize(Stream& s) const
    {
        SerializeReferral(*this, s);
    }


    template <typename Stream>
    inline void Unserialize(Stream& s)
    {
        UnserializeReferral(*this, s);
    }

    template <typename Stream>
    MutableReferral(deserialize_type, Stream& s)
    {
        Unserialize(s);
    }

    std::string GetAlias() const;

    /**
     * Compute the hash of this MutableReferral. This is computed on the
     * fly, as opposed to GetHash() in Referral, which uses a cached result.
     */
    uint256 GetHash() const;

    friend bool operator==(const MutableReferral& a, const MutableReferral& b)
    {
        return a.GetHash() == b.GetHash();
    }

    template <typename Stream, typename RefType>
    friend inline void SerializeReferral(const RefType& ref, Stream& s);

    template <typename Stream, typename RefType>
    friend void UnserializeReferral(RefType& ref, Stream& s);

};

using ReferralRef =  std::shared_ptr<const Referral>;
using ReferralRefs = std::vector<ReferralRef>;

/**
 * Basic referral serialization format:
 *
 * Extended referral serialization format:
 */
template <typename Stream, typename RefType>
inline void UnserializeReferral(RefType& ref, Stream& s)
{
    s >> ref.version;
    s >> ref.parentAddress;
    s >> ref.addressType;
    s >> ref.address;
    s >> ref.pubkey;
    s >> ref.signature;
    if (ref.version >= Referral::INVITE_VERSION) {
        s >> LIMITED_STRING(ref.alias, MAX_ALIAS_LENGTH);
    }

    if(!ref.pubkey.IsValid()) {
        throw std::runtime_error{"invalid referral pubkey"};
    }
}

template <typename Stream, typename RefType>
inline void SerializeReferral(const RefType& ref, Stream& s)
{
    assert(ref.pubkey.IsValid());
    assert(ref.alias.size() <= MAX_ALIAS_LENGTH);

    s << ref.version;
    s << ref.parentAddress;
    s << ref.addressType;
    s << ref.address;
    s << ref.pubkey;
    s << ref.signature;
    if (ref.version >= Referral::INVITE_VERSION) {
        s << LIMITED_STRING(ref.alias, MAX_ALIAS_LENGTH);
    }
}

typedef std::shared_ptr<const Referral> ReferralRef;

template <typename Ref>
static inline ReferralRef MakeReferralRef(Ref&& referralIn)
{
    return std::make_shared<const Referral>(std::forward<Ref>(referralIn));
}

/**
 * Trim an cleanup the alias text. Trims whitespace and removes the '@' symbol.
 */
void CleanupAlias(std::string& alias);

/**
 * Returns true if the referral's alias passes validation.
 * It must not be greater than a certain size and not use certain
 * blacklisted words
 */
bool CheckReferralAlias(std::string alias, bool normalize_alias);

/**
 * Safe version of CheckReferralAlias that assumes the new safety rules.
 */
bool CheckReferralAliasSafe(std::string alias);

/**
 * Normalizes an alias to a form that can compared and stored.
 * This function is safe to handle any user input. It does not validate
 * if the alias is a valid one, you must use CheckReferralAlias after
 * normalization to decide if the alias is valid.
 */
void NormalizeAlias(std::string& alias);

/**
 * Returns true if the two aliases are equal. The aliases are compared
 * in normalize form. Also a transpose check is done on either side.
 * unless safe mode off, then it's just a byte compare.
 */
bool AliasesEqual(std::string a, std::string b, bool safe);

} //namespace referral


#endif // MERIT_PRIMITIVES_REFERRAL_H
