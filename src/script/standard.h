// Copyright (c) 2014-2017 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_SCRIPT_STANDARD_H
#define MERIT_SCRIPT_STANDARD_H

#include "script/interpreter.h"
#include "uint256.h"

#include <boost/variant.hpp>

#include <initializer_list>
#include <stdint.h>

static const bool DEFAULT_ACCEPT_DATACARRIER = true;

class CKeyID;
class CScript;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    CScriptID(const CScript& in);
    explicit CScriptID(const uint160& in) : uint160(in) {}
};

/** A reference to a Parameterized CScript: the Hash160 of its serialization (see script.h) */
class CParamScriptID : public uint160
{
public:
    CParamScriptID() : uint160() {}
    CParamScriptID(const CScript& in);
    explicit CParamScriptID(const uint160& in) : uint160(in) {}
};

/**
 * Default setting for nMaxDatacarrierBytes. 80 bytes of data, +1 for OP_RETURN,
 * +2 for the pushdata opcodes.
 */
static const unsigned int MAX_OP_RETURN_RELAY = 83;

/**
 * A data carrying output is an unspendable output containing data. The script
 * type is designated as TX_NULL_DATA.
 */
extern bool fAcceptDatacarrier;

/** Maximum size of TX_NULL_DATA scripts that this node considers standard. */
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added, such as a soft-fork to enforce
 * strict DER encoding.
 *
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

enum txnouttype
{
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_PARAMETERIZED_SCRIPTHASH,
    TX_MULTISIG,
    TX_EASYSEND,
    TX_NULL_DATA, //!< unspendable OP_RETURN script that carries data
    TX_WITNESS_V0_SCRIPTHASH,
    TX_WITNESS_V0_KEYHASH,
};

class CNoDestination {
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b) { return true; }
    friend bool operator<(const CNoDestination &a, const CNoDestination &b) { return true; }
};

/**
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  * CParamScriptID: TX_PARAMETERIZED_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a merit address
 */
typedef boost::variant<CNoDestination, CKeyID, CScriptID, CParamScriptID> CTxDestination;

/** returns a numberical type based on destination */
char AddressTypeFromDestination(const CTxDestination&);

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest);

/** returns the uin160 from the CKeyID, CScriptID, or CParamScriptID if it is a valid destination */
bool GetUint160(const CTxDestination& dest, uint160& addr);

/** Get the name of a txnouttype as a C string, or nullptr if unknown. */
const char* GetTxnOutputType(txnouttype t);


/**
 *  PubKeys or Hashes returned in Solver
 */
using Solutions = std::vector<std::vector<unsigned char> >;

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on
 * the type. For example, for a P2SH script, vSolutionsRet will contain the
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  typeRet        The script type
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     True if script matches standard template
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, Solutions& vSolutionsRet);

/**
 * Parse a standard scriptPubKey for the destination address. Assigns result to
 * the addressRet parameter and returns true if successful. For multisig
 * scripts, instead use ExtractDestinations. Currently only works for P2PK,
 * P2PKH, and P2SH scripts.
 */
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet, txnouttype& whichTypeRet);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);

/**
 * Parse a standard scriptPubKey with one or more destination addresses. For
 * multisig scripts, this populates the addressRet vector with the pubkey IDs
 * and nRequiredRet with the n required to spend. For other destinations,
 * addressRet is populated with a single value and nRequiredRet is set to 1.
 * Returns true if successful. Currently does not extract address from
 * pay-to-witness scripts.
 */
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

/**
 * Generate a Merit scriptPubKey for the given CTxDestination. Returns a P2PKH
 * script for a CKeyID destination, a P2SH script for a CScriptID, and an empty
 * script for CNoDestination. CParamScriptID's are currently not supported
 */
CScript GetScriptForDestination(const CTxDestination& dest);

/**
 * Generates a Easy Send Script to the receiver specified.
 * An easy send script allows funds to be recoverable by the sender.
 */
CScript GetScriptForEasySend(
        int max_block_height,
        const CPubKey& sender,
        const CPubKey& receiver);

/**
 * Constructs a vault script which limits spending. Allows resetting and
 * changing rules. Returns a parameterized-pay-to-script-hash. 
 */
CScript GetScriptForSimpleVault(const uint160& tag);

/**
 * Constructs a vault script which limits spending and requires multiple signatures. 
 * Allows resetting and changing rules. 
 * Returns a parameterized-pay-to-script-hash.
 */
CScript GetScriptForMultisigVault(const uint160& tag);

/**
 * Constructs a Parameterized P2SH. You can push params onto script after calling
 * this.
 */
CScript GetParameterizedP2SH(const CParamScriptID& dest);

/**
 * This can be used to expand a vector of elements in the GetParameterizedP2SH
 * like so.
 *
 * Example:
 *      std::vector<int> a = { 1,2,3};
 *      GetParameterizedP2SH(destination, param1, param2, ExpandParam(a));
 *
 * is the same as
 *
 *      GetParameterizedP2SH(destination, param1, param2, 1, 2, 3);
 *
 */
template <class Elem>
struct ExpandParamT {
    using Vec = std::vector<Elem>;
    Vec params;

    explicit ExpandParamT(const Vec& v) : params(v) {}
};

template<class Elem>
ExpandParamT<Elem> ExpandParam(const std::vector<Elem>& v)
{
    return ExpandParamT<Elem>(v);
}

namespace details
{
    void AppendParameterizedP2SHTrampoline(CScript&, size_t&);

    template <class... Params>
    void AppendParameterizedP2SHTrampoline(
            CScript& script,
            size_t& size,
            Params... ps);

    template <class Elem, class... Params>
    void AppendParameterizedP2SH(
            CScript& script,
            size_t& size,
            const ExpandParamT<Elem>& v,
            Params... ps)
    {
        for(const auto& e : v.params) {
            size++;
            script << e;
        }
        AppendParameterizedP2SHTrampoline(script, size, ps...);
    }

    template <class Param, class... Params>
    void AppendParameterizedP2SH(
            CScript& script,
            size_t& size,
            const Param& p,
            Params... ps)
    {
        size++;
        script << p;
        AppendParameterizedP2SHTrampoline(script, size, ps...);
    }

    template <class... Params>
    void AppendParameterizedP2SHTrampoline(
            CScript& script,
            size_t& size,
            Params... ps)
    {
        AppendParameterizedP2SH(script, size, ps...);
    }
}

CScript GetParameterizedP2SH(const CParamScriptID& dest);

template <class... Params>
CScript GetParameterizedP2SH(const CParamScriptID& dest, Params... ps)
{
    auto script = GetParameterizedP2SH(dest);
    size_t size = 0;
    details::AppendParameterizedP2SHTrampoline(script, size, ps...);
    script << OP_DEPTH << size << OP_GREATERTHANOREQUAL;
    return script;
}


/** Generate a P2PK script for the given pubkey. */
CScript GetScriptForRawPubKey(const CPubKey& pubkey);

/** Generate a multisig script. */
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

/**
 * Generate a pay-to-witness script for the given redeem script. If the redeem
 * script is P2PK or P2PKH, this returns a P2WPKH script, otherwise it returns a
 * P2WSH script.
 */
CScript GetScriptForWitness(const CScript& redeemscript);

/**
 * Combines the hashes and then hashes them.
 */
void MixAddresses(const uint160& a, const uint160& b, uint160& res);

#endif // MERIT_SCRIPT_STANDARD_H
