// Copyright (c) 2014-2017 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "pubkey.h"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"
#include "core_io.h"


typedef std::vector<unsigned char> valtype;

bool fAcceptDatacarrier = DEFAULT_ACCEPT_DATACARRIER;
unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

CParamScriptID::CParamScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_PARAMETERIZED_SCRIPTHASH: return "parameterized_scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_EASYSEND: return "easysend";
    case TX_NULL_DATA: return "nulldata";
    case TX_WITNESS_V0_KEYHASH: return "witness_v0_keyhash";
    case TX_WITNESS_V0_SCRIPTHASH: return "witness_v0_scripthash";
    }
    return nullptr;
}

bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, Solutions& vSolutionsRet)
{
    // Templates
    static std::multimap<txnouttype, CScript> mTemplates;
    if (mTemplates.empty())
    {
        // Standard tx, sender provides pubkey, receiver adds signature
        mTemplates.insert(std::make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));

        // Merit address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        mTemplates.insert(std::make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        // Sender provides N pubkeys, receivers provides M signatures
        mTemplates.insert(std::make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));

        // Sender provides one of N signatures
        mTemplates.insert(std::make_pair(TX_EASYSEND, CScript() << OP_INTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_EASYSEND));
    }

    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash or paramed-pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    // or OP_HASH160 20 [20 byte hash] OP_EQUALVERIFY [param1] [param2] ...
    const bool is_pay_to_script_hash =  scriptPubKey.IsPayToScriptHash();
    const bool is_parameterized_pay_to_script_hash = scriptPubKey.IsParameterizedPayToScriptHash();
    if (is_pay_to_script_hash || is_parameterized_pay_to_script_hash)
    {
        typeRet = is_pay_to_script_hash ? TX_SCRIPTHASH : TX_PARAMETERIZED_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == 20) {
            typeRet = TX_WITNESS_V0_KEYHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        if (witnessversion == 0 && witnessprogram.size() == 32) {
            typeRet = TX_WITNESS_V0_SCRIPTHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        return false;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        typeRet = TX_NULL_DATA;
        return true;
    }

    // Scan templates
    const CScript& script1 = scriptPubKey;
    for (const std::pair<txnouttype, CScript>& tplate : mTemplates)
    {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        std::vector<unsigned char> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TX_MULTISIG)
                {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS)
            {
                while (vch1.size() >= 33 && vch1.size() <= 65)
                {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY)
            {
                if (vch1.size() < 33 || vch1.size() > 65)
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH)
            {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER)
            {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 ||
                    (opcode1 >= OP_1 && opcode1 <= OP_16))
                {
                    char n = (char)CScript::DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(valtype(1, n));
                }
                else
                    break;
            }
            else if (opcode2 == OP_INTEGER)
            {   
                if (opcode1 == OP_0 ||
                    (opcode1 >= 0 && opcode1 <= OP_PUSHDATA4)) {
                    vSolutionsRet.push_back(vch1);
                }
                else
                    break;
            }
            else if (opcode1 != opcode2 || vch1 != vch2)
            {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

char AddressTypeFromDestination(const CTxDestination& dest)
{
    char addressType = 0;
    if(auto id = boost::get<CKeyID>(&dest)) {
        addressType = 1;
    } else if(auto id = boost::get<CScriptID>(&dest)) {
        addressType = 2;
    } else if(auto id = boost::get<CParamScriptID>(&dest)) {
        addressType = 3;
    }

    return addressType;
}


bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
    else if (whichType == TX_PUBKEYHASH)
    {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_SCRIPTHASH)
    {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_PARAMETERIZED_SCRIPTHASH)
    {
        addressRet = CParamScriptID(uint160(vSolutions[0]));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

void ExtractDestinationsFromSolutions(
        std::vector<valtype>::const_iterator first,
        std::vector<valtype>::const_iterator last,
        std::vector<CTxDestination>& ret)
{
    for(; first != last; first++) {
        CPubKey pub_key(*first);
        if (!pub_key.IsValid()) continue;
        ret.push_back(pub_key.GetID());
    }
}

bool ExtractDestinations(const CScript& scriptPubKey,
        txnouttype& typeRet,
        std::vector<CTxDestination>& addressRet,
        int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA){
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG) {
        nRequiredRet = vSolutions.front()[0];
        ExtractDestinationsFromSolutions(
                vSolutions.begin()+1,
                vSolutions.begin()+(vSolutions.size()-1),
                addressRet);

    } else if (typeRet == TX_EASYSEND) {
        nRequiredRet = 1;
        ExtractDestinationsFromSolutions(
                vSolutions.begin(),
                vSolutions.end(),
                addressRet);

    } else {
        nRequiredRet = 1;
        CTxDestination address;
        if (ExtractDestination(scriptPubKey, address))
            addressRet.push_back(address);
    }

    return !addressRet.empty();
}

namespace
{
class CScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
public:
    explicit CScriptVisitor(CScript *scriptin) { script = scriptin; }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }

    bool operator()(const CParamScriptID &scriptID) const {
        //TODO: Must do lookup for params on blockchain/mempool based on script ID.
        //The assumption is that all unspent coins with same id have same params.
        throw std::invalid_argument("Parameterized script ids are not supported yet");
        return false;
    }
};
} // namespace

CScript GetScriptForDestination(const CTxDestination& dest)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

CScript GetScriptForEasySend(
        int max_block_height,
        const CPubKey& sender,
        const CPubKey& receiver)
{
    return CScript() 
        << max_block_height
        << ToByteVector(receiver) 
        << ToByteVector(sender)  //sender key is allowed to recieve funds after
                                 //max_block_height is met
        << CScript::EncodeOP_N(2) 
        << OP_EASYSEND;
}

CScript GetScriptForSimpleVault(const uint160& tag)
{
    // params <spend key> <renew key> [addresses: <addr1> <addr2> <...> <num addresses>] <tag> <vault type>
    // stack on start0:  <sig> <mode> <spend key> <renew key> [addresses] <tag> |
    CScript script;
    script
        << OP_DROP                      // <sig> <mode> <spend key> <renew key> [addresses] <tag>| 
        << OP_DROP                      // <sig> <mode> <spend key> <renew key> [addresses] | 
        << OP_NTOALTSTACK               // <out index> <sig> <mode> | [addresses]
        << OP_TOALTSTACK                // <sig> <mode> <spend key> | [addresses] <renew key>
        << OP_TOALTSTACK                // <sig> <mode> | [addresses] <renew key> <spend key>
        << 0                            // <sig> <mode> 0 | [addresses] <renew key> <spend key>
        << OP_EQUAL                     // <sig> <bool> | [addresses] <renew key> <spend key>
        << OP_IF                        // <sig> | [addresses] <renew key> <spend key>
        <<      OP_FROMALTSTACK         // <sig> <spend key> | [addresses] <renew key>
        <<      OP_DUP                  // <sig> <spend key> <spend key> | [addresses] <renew key>
        <<      OP_TOALTSTACK           // <sig> <spend key> | [addresses] <renew key> <spend key>
        <<      OP_CHECKSIGVERIFY       // | [addresses] <renew key> <spend key>
        <<      OP_FROMALTSTACK         // <spend key> | [addresses] <renew key>
        <<      OP_FROMALTSTACK         // <spend key> <renew key> | [addresses]
        <<      0                       // <spend key> <renew key> <0 args> | [addresses]
        <<      0                       // <spend key> <renew key> <0 args> <out index>| [addresses]
        <<      OP_NFROMALTSTACK        // <spend key> <renew key> <0 args> <out index> [addresses] |
        <<      OP_NDUP                 // <spend key> <renew key> <0 args> <out index> [addresses] [addresses] |
        <<      OP_NTOALTSTACK          // <spend key> <renew key> <0 args> <out index> [addresses] | [addresses]
        <<      OP_CHECKOUTPUTSIGVERIFY // <spend key> <renew key> | [addresses]
        <<      OP_NFROMALTSTACK        // <spend key> <renew key> [addresses] |
        <<      OP_DUP                  // <spend key> <renew key> [addresses] <num addresss> |
        <<      5                       // <spend key> <renew key> [addresses] <num addresss> 4 |
        <<      OP_ADD                  // <spend key> <renew key> [addresses] <total args> |
        <<      OP_TOALTSTACK           // <spend key> <renew key> [addresses] | <total args>
        <<      ToByteVector(tag)       // <spend key> <renew key> [addresses] <tag> | 
        <<      0                       // <spend key> <renew key> [addresses] <tag> <vault type> |
        <<      OP_FROMALTSTACK         // <spend key> <renew key> [addresses] <tag> <vault type> <total args> | 
        <<      1                       // <spend key> <renew key> [addresses] <tag> <vault type> <total args> <out index> |
        <<      's'                     // <spend key> <renew key> [addresses] <tag> <vault type> <total args> <out index> <self> |
        <<      1                       // <spend key> <renew key> [addresses] <tag> <vault type> <total args> <out index> <self> <num addresses>|
        <<      OP_CHECKOUTPUTSIGVERIFY // |
        <<      2                       // 2 |
        <<      OP_OUTPUTCOUNT          // <count>
        <<      OP_EQUAL                // <bool>
        << OP_ELSE
        <<      OP_FROMALTSTACK         // <sig> <spend key> | [addresses] <renew key>
        <<      OP_DROP                 // <sig> | [addresses] <renew key>
        <<      OP_FROMALTSTACK         // <sig> <renew key> | [addresses]  
        <<      OP_CHECKSIGVERIFY       // | [addresses]
        <<      0                       // <total args> | [addresses]
        <<      0                       // <total args> <out index> | [addresses]
        <<      's'                     // <total args> <out index> <self> | [addresses]
        <<      1                       // <total args> <out index> <self> <num addresses>| [addresses]
        <<      OP_CHECKOUTPUTSIGVERIFY //  | [addresses]
        <<      1                       // 1 | [addresses]
        <<      OP_OUTPUTCOUNT          // 1 <count> | [addresses]
        <<      OP_EQUAL                // <bool> | [addresses]
        << OP_ENDIF;

    return script;
}

namespace details
{
    void AppendParameterizedP2SHTrampoline(CScript&, size_t&)
    {
        // nothing to append
    }
}

CScript GetParameterizedP2SH(const CParamScriptID& dest)
{
    CScript script;
    script << OP_HASH160 << ToByteVector(dest) << OP_EQUALVERIFY;
    return script;
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    for (const CPubKey& key : keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}

CScript GetScriptForWitness(const CScript& redeemscript)
{
    CScript ret;

    txnouttype typ;
    std::vector<std::vector<unsigned char> > vSolutions;
    if (Solver(redeemscript, typ, vSolutions)) {
        if (typ == TX_PUBKEY) {
            unsigned char h160[20];
            CHash160().Write(&vSolutions[0][0], vSolutions[0].size()).Finalize(h160);
            ret << OP_0 << std::vector<unsigned char>(&h160[0], &h160[20]);
            return ret;
        } else if (typ == TX_PUBKEYHASH) {
           ret << OP_0 << vSolutions[0];
           return ret;
        }
    }
    uint256 hash;
    CSHA256().Write(&redeemscript[0], redeemscript.size()).Finalize(hash.begin());
    ret << OP_0 << ToByteVector(hash);
    return ret;
}

bool IsValidDestination(const CTxDestination& dest)
{
    return dest.which() != 0;
}

bool GetUint160(const CTxDestination& dest, uint160& addr)
{
    if(!IsValidDestination(dest)) {
        return false;
    }

    if(auto key_id = boost::get<CKeyID>(&dest)) {
        addr = *key_id;
    } else if (auto script_id = boost::get<CScriptID>(&dest)) { 
        addr = *script_id;
    } else if (auto script_id = boost::get<CParamScriptID>(&dest)) { 
        addr = *script_id;
    } else { 
        assert(false && "forgot to implement a case");
    }

    return true;
}
