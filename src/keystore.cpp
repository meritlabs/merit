// Copyright (c) 2017-2019 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "keystore.h"

#include "key.h"
#include "pubkey.h"
#include "util.h"

bool CKeyStore::AddKey(const CKey &key) {
    return AddKeyPubKey(key, key.GetPubKey());
}

bool CBasicKeyStore::GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const
{
    CKey key;
    if (!GetKey(address, key)) {
        LOCK(cs_KeyStore);
        WatchKeyMap::const_iterator it = mapWatchKeys.find(address);
        if (it != mapWatchKeys.end()) {
            vchPubKeyOut = it->second;
            return true;
        }
        return false;
    }
    vchPubKeyOut = key.GetPubKey();
    return true;
}

bool CBasicKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    LOCK(cs_KeyStore);
    mapKeys[pubkey.GetID()] = key;
    return true;
}

bool CBasicKeyStore::AddCScript(const CScript& redeemScript, const uint160& address)
{
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
        return error("CBasicKeyStore::AddCScript(): redeemScripts > %i bytes are invalid", MAX_SCRIPT_ELEMENT_SIZE);

    LOCK(cs_KeyStore);
    mapScripts[CScriptID(address)] = redeemScript;
    return true;
}

bool CBasicKeyStore::HaveCScript(const CScriptID& hash) const
{
    LOCK(cs_KeyStore);
    return mapScripts.count(hash) > 0;
}

bool CBasicKeyStore::GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const
{
    LOCK(cs_KeyStore);
    ScriptMap::const_iterator mi = mapScripts.find(hash);
    if (mi == mapScripts.end()) {
        return false;
    }

    redeemScriptOut = mi->second;
    return true;
}

bool CBasicKeyStore::AddParamScript(const CScript& redeemScript, const uint160& address)
{
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
        return error("CBasicKeyStore::AddCScript(): redeemScripts > %i bytes are invalid", MAX_SCRIPT_ELEMENT_SIZE);

    LOCK(cs_KeyStore);
    mapParamScripts[CParamScriptID(address)] = redeemScript;
    return true;
}

bool CBasicKeyStore::HaveParamScript(const CParamScriptID& hash) const
{
    LOCK(cs_KeyStore);
    return mapParamScripts.count(hash) > 0;
}

bool CBasicKeyStore::GetParamScript(const CParamScriptID &hash, CScript& redeemScriptOut) const
{
    LOCK(cs_KeyStore);
    ParamScriptMap::const_iterator mi = mapParamScripts.find(hash);
    if (mi == mapParamScripts.end()) {
        return false;
    }

    redeemScriptOut = mi->second;
    return true;
}

bool CBasicKeyStore::AddReferralAddressPubKey(const uint160& address, const CKeyID& pubkey_id)
{
    LOCK(cs_KeyStore);
    mapReferralAddresses[address] = pubkey_id;

    return true;
}

bool CBasicKeyStore::CBasicKeyStore::HaveReferralAddressPubKey(const uint160& address) const
{
    LOCK(cs_KeyStore);

    return mapReferralAddresses.count(address) > 0;
}

bool CBasicKeyStore::CBasicKeyStore::GetReferralAddressPubKey(const uint160& address, CKeyID& pubkey_id_out) const
{
    LOCK(cs_KeyStore);
    auto mi = mapReferralAddresses.find(address);
    if (mi == mapReferralAddresses.end()) {
        return false;
    }

    pubkey_id_out = mi->second;
    return true;
}

static bool ExtractPubKey(const CScript &dest, CPubKey& pubKeyOut)
{
    //TODO: Use Solver to extract this?
    CScript::const_iterator pc = dest.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;
    if (!dest.GetOp(pc, opcode, vch) || vch.size() < 33 || vch.size() > 65)
        return false;
    pubKeyOut = CPubKey(vch);
    if (!pubKeyOut.IsFullyValid())
        return false;
    if (!dest.GetOp(pc, opcode, vch) || opcode != OP_CHECKSIG || dest.GetOp(pc, opcode, vch))
        return false;
    return true;
}

bool CBasicKeyStore::AddWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.insert(dest);
    CPubKey pubKey;
    if (ExtractPubKey(dest, pubKey))
        mapWatchKeys[pubKey.GetID()] = pubKey;
    return true;
}

bool CBasicKeyStore::RemoveWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.erase(dest);
    CPubKey pubKey;
    if (ExtractPubKey(dest, pubKey))
        mapWatchKeys.erase(pubKey.GetID());
    return true;
}

bool CBasicKeyStore::HaveWatchOnly(const CScript &dest) const
{
    LOCK(cs_KeyStore);
    return setWatchOnly.count(dest) > 0;
}

bool CBasicKeyStore::HaveWatchOnly() const
{
    LOCK(cs_KeyStore);
    return (!setWatchOnly.empty());
}
