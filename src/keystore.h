// Copyright (c) 2017-2019 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_KEYSTORE_H
#define MERIT_KEYSTORE_H

#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"

#include <boost/signals2/signal.hpp>

/** A virtual base class for key stores */
class CKeyStore
{
protected:
    mutable CCriticalSection cs_KeyStore;

public:
    virtual ~CKeyStore() {}

    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual void GetKeys(std::set<CKeyID> &setAddress) const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const =0;

    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript, const uint160&) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    virtual bool AddParamScript(const CScript& redeemScript, const uint160&) =0;
    virtual bool HaveParamScript(const CParamScriptID &hash) const =0;
    virtual bool GetParamScript(const CParamScriptID &hash, CScript& redeemScriptOut) const =0;

    // Support for beaconed scripts addresses mixed with signer pubkey
    virtual bool AddReferralAddressPubKey(const uint160& address, const CKeyID& pubkey_id) =0;
    virtual bool HaveReferralAddressPubKey(const uint160& address) const =0;
    virtual bool GetReferralAddressPubKey(const uint160& address, CKeyID& pubkey_id_out) const =0;

    //! Support for Watch-only addresses
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveWatchOnly() const =0;
};

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CKeyID, CPubKey> WatchKeyMap;
typedef std::map<CScriptID, CScript > ScriptMap;
typedef std::map<CParamScriptID, CScript > ParamScriptMap;
typedef std::map<uint160, CKeyID> ReferralAddressMap;
typedef std::set<CScript> WatchOnlySet;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
    KeyMap mapKeys;
    WatchKeyMap mapWatchKeys;
    ScriptMap mapScripts;
    ParamScriptMap mapParamScripts;
    ReferralAddressMap mapReferralAddresses;
    WatchOnlySet setWatchOnly;

public:
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    bool HaveKey(const CKeyID &address) const override
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
    void GetKeys(std::set<CKeyID> &setAddress) const override
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.begin();
            while (mi != mapKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }
    bool GetKey(const CKeyID &address, CKey &keyOut) const override
    {
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut = mi->second;
                return true;
            }
        }
        return false;
    }
    bool AddCScript(const CScript& redeemScript, const uint160&) override;
    bool HaveCScript(const CScriptID &hash) const override;
    bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const override;

    bool AddParamScript(const CScript& redeemScript, const uint160&) override;
    bool HaveParamScript(const CParamScriptID &hash) const override;
    bool GetParamScript(const CParamScriptID &hash, CScript& redeemScriptOut) const override;

    bool AddReferralAddressPubKey(const uint160& address, const CKeyID& pubkey_id) override;
    bool HaveReferralAddressPubKey(const uint160& address) const override;
    bool GetReferralAddressPubKey(const uint160& address, CKeyID& pubkey_id_out) const override;

    bool AddWatchOnly(const CScript &dest) override;
    bool RemoveWatchOnly(const CScript &dest) override;
    bool HaveWatchOnly(const CScript &dest) const override;
    bool HaveWatchOnly() const override;
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;
typedef std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char> > > CryptedKeyMap;

#endif // MERIT_KEYSTORE_H
