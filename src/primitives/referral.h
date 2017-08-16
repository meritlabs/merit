// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_REFERRAL_H
#define BITCOIN_PRIMITIVES_REFERRAL_H

#include <stdint.h>
#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"
#include "net.h"
#include "util.h"

struct CMutableReferral;

static const int SERIALIZE_REFERRAL = 0x40000000;

/**
 * Basic referral serialization format:
 *
 * Extended referral serialization format:
 */
template<typename Stream, typename TxType>
inline void UnserializeReferral(TxType& ref, Stream& s) {
    s >> ref.nVersion;
    unsigned char flags = 0;
    s >> ref.previousReferral;
    s >> ref.scriptSig;
}

template<typename Stream, typename TxType>
inline void SerializeReferral(const TxType& ref, Stream& s) {
    s << ref.nVersion;
    s << ref.previousReferral;
    s << ref.scriptSig;
}


/** The basic referral that is broadcast on the network and contained in
 * blocks. A referral references a previous referral which helps construct the
 * referral tree.
 */
class CReferral
{
public:
    // Default referral version.
    static const int32_t CURRENT_VERSION=0;

    // Changing the default referral version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION=0;

    const int32_t nVersion;
    uint256 previousReferral;

    // Signed signature of previousReferral + nVersion
    CScript scriptSig;

private:
    /** Memory only. */
    const uint256 hash;

    uint256 ComputeHash() const;

public:
    /** Construct a CReferral that qualifies as IsNull() */
    CReferral();

    /** Convert a CMutableReferral into a CReferral. */
    CReferral(const CMutableReferral &ref);
    CReferral(CMutableReferral &&ref);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeReferral(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    CReferral(deserialize_type, Stream& s) : CReferral(CMutableReferral(deserialize, s)) {}

    const uint256& GetHash() const {
        return hash;
    }

    // Compute a hash that includes both transaction and witness data
    uint256 GetWitnessHash() const;

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool RelayWalletTransaction(CConnman* connman);

    friend bool operator==(const CReferral& a, const CReferral& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CReferral& a, const CReferral& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;
};

/** A mutable version of CReferral. */
struct CMutableReferral
{
    const int32_t nVersion;
    uint256 previousReferral;
    CScript scriptSig;

    CMutableReferral();
    CMutableReferral(const CReferral& ref);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeReferral(*this, s);
    }


    template <typename Stream>
    inline void Unserialize(Stream& s) {
        UnserializeReferral(*this, s);
    }

    template <typename Stream>
    CMutableReferral(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableReferral. This is computed on the
     * fly, as opposed to GetHash() in CReferral, which uses a cached result.
     */
    uint256 GetHash() const;

    friend bool operator==(const CMutableReferral& a, const CMutableReferral& b)
    {
        return a.GetHash() == b.GetHash();
    }
};

typedef std::shared_ptr<const CReferral> CReferralRef;
static inline CReferralRef MakeReferralRef() { return std::make_shared<const CReferral>(); }
template <typename Ref> static inline CReferralRef MakeReferralRef(Ref&& previousRef) { return std::make_shared<const CReferral>(std::forward<Ref>(previousRef)); }

#endif // BITCOIN_PRIMITIVES_REFERRAL_H
