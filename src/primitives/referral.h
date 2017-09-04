// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_REFERRAL_H
#define BITCOIN_PRIMITIVES_REFERRAL_H

#include <stdint.h>
#include "pubkey.h"
#include "serialize.h"
#include "uint256.h"

struct MutableReferral;

static const int SERIALIZE_REFERRAL = 0x40000000;

/**
 * Basic referral serialization format:
 *
 * Extended referral serialization format:
 */
template<typename Stream, typename TxType>
inline void UnserializeReferral(TxType& ref, Stream& s) {
    s >> ref.m_previousReferral;
    s >> ref.m_pubKeyId;
    s >> ref.m_codeHash;
}

template<typename Stream, typename TxType>
inline void SerializeReferral(const TxType& ref, Stream& s) {
    s << ref.m_previousReferral;
    s << ref.m_pubKeyId;
    s << ref.m_codeHash;
}

/** The basic referral that is broadcast on the network and contained in
 * blocks. A referral references a previous referral which helps construct the
 * referral tree.
 */
class Referral
{
public:
    // Default referral version.
    static const int32_t CURRENT_VERSION=0;

    // Changing the default referral version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION=0;

    const int32_t m_nVersion;

    // hash code of revious referral
    uint256 m_previousReferral;

    // address that this referral is related to
    CKeyID m_pubKeyId;

    // Referral code that is used as a referrence to a wallet
    const std::string m_code;

    // hash of m_code
    const uint256 m_codeHash;

private:
    /** Memory only. */
    const uint256 m_hash;

    uint256 ComputeHash() const;

public:
    Referral(CKeyID& addressIn, uint256 referralIn);

    /** Convert a MutableReferral into a Referral. */
    Referral(const MutableReferral &ref);
    Referral(MutableReferral &&ref);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeReferral(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    Referral(deserialize_type, Stream& s) : Referral(MutableReferral(deserialize, s)) {}

    const uint256& GetHash() const {
        return m_hash;
    }

    // Compute a m_hash that includes both transaction and witness data
    uint256 GetWitnessHash() const;

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    friend bool operator==(const Referral& a, const Referral& b)
    {
        return a.m_hash == b.m_hash;
    }

    friend bool operator!=(const Referral& a, const Referral& b)
    {
        return a.m_hash != b.m_hash;
    }

    std::string ToString() const;
};

/** A mutable version of Referral. */
struct MutableReferral
{
    int32_t m_nVersion;
    uint256 m_previousReferral;
    CKeyID m_pubKeyId;
    std::string m_code;
    uint256 m_codeHash;

    MutableReferral() { }
    MutableReferral(CKeyID& addressIn, uint256 referralIn);
    MutableReferral(const Referral& ref);

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
    MutableReferral(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    /** Compute the hash of this MutableReferral. This is computed on the
     * fly, as opposed to GetHash() in Referral, which uses a cached result.
     */
    uint256 GetHash() const;

    uint256 GetCodeHash() const;

    friend bool operator==(const MutableReferral& a, const MutableReferral& b)
    {
        return a.GetHash() == b.GetHash();
    }
};

typedef std::shared_ptr<const Referral> ReferralRef;

static inline ReferralRef MakeReferralRef(CKeyID& addressIn, uint256 referralCodeHashIn)
{
    return std::make_shared<const Referral>(addressIn, referralCodeHashIn);
}

template <typename Ref> static inline ReferralRef MakeReferralRef(Ref&& referralIn)
{
     return std::make_shared<const Referral>(std::forward<Ref>(referralIn));
}

#endif // BITCOIN_PRIMITIVES_REFERRAL_H
