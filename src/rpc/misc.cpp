// Copyright (c) 2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "clientversion.h"
#include "core_io.h"
#include "init.h"
#include "validation.h"
#include "httpserver.h"
#include "net.h"
#include "netbase.h"
#include "rpc/blockchain.h"
#include "rpc/misc.h"
#include "rpc/server.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"

#include "rpc/safemode.h"
#include "pog/anv.h"

#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include "warnings.h"

#include <stdint.h>
#include <numeric>
#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

#include <univalue.h>

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getinfo\n"
            "\nDEPRECATED. Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"deprecation-warning\": \"...\" (string) warning that the getinfo command is deprecated and will be removed in 0.16\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total merit balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",       (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since Unix epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in " + CURRENCY_UNIT + "/kB\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for transactions in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": \"...\"             (string) any error messages\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getinfo", "")
            + HelpExampleRpc("getinfo", "")
        );

#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("deprecation-warning", "WARNING: getinfo is deprecated and will be fully removed in 0.16."
        " Projects should transition to using getblockchaininfo, getnetworkinfo, and getwalletinfo before upgrading to 0.16"));
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.push_back(Pair("walletversion", pwallet->GetVersion()));
        obj.push_back(Pair("balance",       ValueFromAmount(pwallet->GetBalance())));
    }
#endif
    obj.push_back(Pair("blocks",        (int)chainActive.Height()));
    obj.push_back(Pair("timeoffset",    GetTimeOffset()));
    if(g_connman)
        obj.push_back(Pair("connections",   (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL)));
    obj.push_back(Pair("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : std::string())));
    obj.push_back(Pair("difficulty",    (double)GetDifficulty()));
    obj.push_back(Pair("testnet",       Params().NetworkIDString() == CBaseChainParams::TESTNET));
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.push_back(Pair("keypoololdest", pwallet->GetOldestKeyPoolTime()));
        obj.push_back(Pair("keypoolsize",   (int)pwallet->GetKeyPoolSize()));
    }
    if (pwallet && pwallet->IsCrypted()) {
        obj.push_back(Pair("unlocked_until", pwallet->nRelockTime));
    }
    obj.push_back(Pair("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK())));
#endif
    obj.push_back(Pair("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    return obj;
}

UniValue validateaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "validateaddress \"address\"\n"
            "\nReturn information about the given merit address.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) The merit address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,         (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"isbeaconed\" : true|false,      (boolean) If the address is beaconed or not.\n"
            "  \"isconfirmed\" : true|false,     (boolean) If the address confirmed or not.\n"
            "  \"address\" : \"address\",        (string) The merit address validated\n"
            "  \"addresstype\" : \"type\",       (string) Type of addres: pubkey, script or parameterized_script\n"
            "  \"alias\" : \"alias\",            (string) Address alias if exists\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,          (boolean) If the address is yours or not\n"
            "  \"iswatchonly\" : true|false,     (boolean) If the address is watchonly\n"
            "  \"timestamp\" : timestamp,        (number, optional) The creation time of the key if available in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"hdkeypath\" : \"keypath\"       (string, optional) The HD keypath if the key is HD and available\n"
            "  \"hdmasterkeyid\" : \"<hash160>\" (string, optional) The Hash160 of the HD master pubkey\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
            + HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
        );

#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif

    CTxDestination dest = LookupDestination(request.params[0].get_str());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        auto address = CMeritAddress{dest};

        bool isBeaconed = CheckAddressBeaconed(address);
        bool isConfirmed = CheckAddressConfirmed(address);

        ret.pushKV("isbeaconed", isBeaconed);
        ret.pushKV("isconfirmed", isConfirmed);
        ret.pushKV("address", address.ToString());
        ret.pushKV("addresstype", address.GetTypeStr());

        if (isBeaconed) {
            const auto referral = prefviewcache->GetReferral(*(address.GetUint160()));
            ret.pushKV("alias", referral ? referral->alias : "");
        }


#ifdef ENABLE_WALLET
        bool is_param_script = boost::get<CParamScriptID>(&dest) != nullptr;
        if(!is_param_script) {
            isminetype mine = pwallet ? IsMine(*pwallet, dest) : ISMINE_NO;
            ret.push_back(Pair("ismine", bool(mine & ISMINE_SPENDABLE)));
            ret.push_back(Pair("iswatchonly", bool(mine & ISMINE_WATCH_ONLY)));
        }
        UniValue detail = boost::apply_visitor(DescribeAddressVisitor(pwallet), dest);
        ret.pushKVs(detail);
        if (pwallet) {
            const auto& meta = pwallet->mapKeyMetadata;
            const auto keyID = boost::get<CKeyID>(&dest);
            auto it = keyID ? meta.find(*keyID) : meta.end();
            if (it == meta.end() && boost::get<CScriptID>(&dest) != nullptr) {
                CScript scriptPubKey = GetScriptForDestination(dest);
                it = meta.find(CScriptID(scriptPubKey));
            }
            if (it != meta.end()) {
                ret.push_back(Pair("timestamp", it->second.nCreateTime));
                if (!it->second.hdKeypath.empty()) {
                    ret.push_back(Pair("hdkeypath", it->second.hdKeypath));
                    ret.push_back(Pair("hdmasterkeyid", it->second.hdMasterKeyID.GetHex()));
                }
            }
        }
#endif
    }
    return ret;
}

UniValue validatealias(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "validatealias \"alias\"\n"
            "\nCheck if given alias is a valid alias.\n"
            "\nArguments:\n"
            "1. \"alias\"  (string, required) An alias for merit address\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\": true|false,   (boolean) If an alias is valid or not.\n"
            "  \"isvacant\": true|false,  (boolean) If an alias is vacant and can be used.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validatealias", "\"awesomealias\"")
            + HelpExampleRpc("validatealias", "\"awesomealias\"")
        );

    UniValue ret(UniValue::VOBJ);

    auto alias = request.params[0].get_str();

    auto dest = DecodeDestination(alias);

    // alias can not be in address format
    bool is_valid = !IsValidDestination(dest);
    is_valid &= referral::CheckReferralAliasSafe(alias);

    // assume and do the new safer rule logic.
    bool is_vacant =
        !mempoolReferral.Exists(alias) && !prefviewcache->Exists(alias, true);

    ret.push_back(Pair("isvalid", is_valid));
    ret.push_back(Pair("isvacant", is_vacant));

    return ret;
}
// Needed even with !ENABLE_WALLET, to pass (ignored) pointers around
class CWallet;

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(CWallet * const pwallet, const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[2].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw std::runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw std::runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw std::runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Merit address and we have full public key:
        CTxDestination dest = LookupDestination(ks);
        if (pwallet && IsValidDestination(dest)) {
            const CKeyID *keyID = boost::get<CKeyID>(&dest);
            if (!keyID) {
                throw std::runtime_error(strprintf("%s does not refer to a key", ks));
            }
            CPubKey vchPubKey;
            if (!pwallet->GetPubKey(*keyID, vchPubKey)) {
                throw std::runtime_error(strprintf("no full public key for address %s", ks));
            }
            if (!vchPubKey.IsFullyValid())
                throw std::runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw std::runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }
        else
        {
            throw std::runtime_error(" Invalid public key: "+ks);
        }
    }

    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw std::runtime_error(
                strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
#else
    CWallet * const pwallet = nullptr;
#endif

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 3)
    {
        std::string msg = "createmultisig nrequired signingaddress [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. signingaddress (string, required) The address of the public key used to sign the beacon for the multisig address.\n"
            "3. \"keys\"       (string, required) A json array of keys which are merit addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) merit address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("createmultisig", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw std::runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    auto signing_dest = DecodeDestination(request.params[1].get_str());
    auto signing_key_id = boost::get<CKeyID>(&signing_dest);
    if(!signing_key_id) {
        std::stringstream e;
        e << "The beacon signing address must be a valid public key address: " << request.params[1].get_str();
        throw std::runtime_error(e.str());
    }

    CScript redeem_script = _createmultisig_redeemScript(pwallet, request.params);

    //We now mix the singing key and the redeem script addresses together to
    //get the final destination address
    CScriptID script_id = redeem_script;
    uint160 mixed_address;
    MixAddresses(script_id, *signing_key_id, mixed_address);
    CScriptID script_address{mixed_address};

    auto output_script = GetScriptForDestination(script_address);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", EncodeDestination(script_address)));
    result.push_back(Pair("signingAddress", EncodeDestination(*signing_key_id)));
    result.push_back(Pair("outputScript", HexStr(output_script)));
    result.push_back(Pair("redeemScriptAddress", EncodeDestination(script_id)));
    result.push_back(Pair("redeemScript", HexStr(redeem_script)));

    return result;
}

UniValue verifymessage(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "verifymessage \"address\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The merit address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\", \"signature\", \"my message\"")
        );

    LOCK(cs_main);

    std::string strAddress  = request.params[0].get_str();
    std::string strSign     = request.params[1].get_str();
    std::string strMessage  = request.params[2].get_str();

    CTxDestination destination = LookupDestination(strAddress);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == *keyID);
}

UniValue verifydata(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 3 && request.params.size() != 4))
        throw std::runtime_error(
            "verifymessage \"data\" \"signature\" \"pubkey\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"data\"         (string, required) Data in HEX that was signed.\n"
            "2. \"signature\"    (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"pubkey\"       (string, required) The Pub Key used to verify the signature.\n"
            "4. \"ishash\"       (bool, optional) If the data is already the hash.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nSign some data\n"
            + HelpExampleCli("signdata", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"KzoE8aAgDYG7KwexBoTvKZurEiWmip41Pws8mReLb8a1u5nKVnn1\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifyhdata", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"signature\" \"03C54754046C5B3FCA19AF3CEA45883F47280954FABF4C7EA0E970EF792D0DEF24\"")
        );

    LOCK(cs_main);

    auto data  = ParseHex(request.params[0].get_str());
    auto sig    = ParseHex(request.params[1].get_str());
    auto pub_key = CPubKey{ParseHex(request.params[2].get_str())};

    auto is_hash = !request.params[3].isNull();

    CHashWriter ss(SER_GETHASH, 0);
    ss << data;

    return pub_key.Verify(is_hash ? uint256{data} : ss.GetHash(), sig);
}

UniValue signdata(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 2 && request.params.size() != 3))
        throw std::runtime_error(
            "signdata \"hexdata\" \"privatekey\"\n"
            "\nSign hex binary data with the private key\n"
            "\nArguments:\n"
            "1. \"data\"         (string, required) Data in HEX to sign using private key.\n"
            "2. \"privatekey\"      (string, required) Private key in WIF format.\n"
            "3. \"ishash\"          (bool, optional) If the hexdata is already the hash.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            "\nCreate the signature\n"
            + HelpExampleCli("signdata", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"KzoE8aAgDYG7KwexBoTvKZurEiWmip41Pws8mReLb8a1u5nKVnn1\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifyhdata", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"signature\" \"03C54754046C5B3FCA19AF3CEA45883F47280954FABF4C7EA0E970EF792D0DEF24\"")
        );

    LOCK(cs_main);

    auto data = ParseHex(request.params[0].get_str());
    CMeritSecret secret;
    secret.SetString(request.params[1].get_str());

    auto is_hash = !request.params[2].isNull();

    auto key = secret.GetKey();

    CHashWriter ss(SER_GETHASH, 0);
    ss << data;

    std::vector<unsigned char> sig;
    if (!key.Sign(is_hash ? uint256{data} : ss.GetHash(), sig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return HexStr(sig);
}


UniValue signmessagewithprivkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "signmessagewithprivkey \"privkey\" \"message\"\n"
            "\nSign a message with the private key of an address\n"
            "\nArguments:\n"
            "1. \"privkey\"         (string, required) The private key to sign the message with.\n"
            "2. \"message\"         (string, required) The message to create a signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nCreate the signature\n"
            + HelpExampleCli("signmessagewithprivkey", "\"privkey\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("signmessagewithprivkey", "\"privkey\", \"my message\"")
        );

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CMeritSecret vchSecret;
    bool fGood = vchSecret.SetString(strPrivkey);
    if (!fGood)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    CKey key = vchSecret.GetKey();
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue setmocktime(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time."
        );

    if (!Params().MineBlocksOnDemand())
        throw std::runtime_error("setmocktime for regression testing (-regtest mode) only");

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsCurrentForFeeEstimation() and IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all call sites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    SetMockTime(request.params[0].get_int64());

    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo()
{
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("used", uint64_t(stats.used)));
    obj.push_back(Pair("free", uint64_t(stats.free)));
    obj.push_back(Pair("total", uint64_t(stats.total)));
    obj.push_back(Pair("locked", uint64_t(stats.locked)));
    obj.push_back(Pair("chunks_used", uint64_t(stats.chunks_used)));
    obj.push_back(Pair("chunks_free", uint64_t(stats.chunks_free)));
    return obj;
}

#ifdef HAVE_MALLOC_INFO
static std::string RPCMallocInfo()
{
    char *ptr = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&ptr, &size);
    if (f) {
        malloc_info(0, f);
        fclose(f);
        if (ptr) {
            std::string rv(ptr, size);
            free(ptr);
            return rv;
        }
    }
    return "";
}
#endif

UniValue getmemoryinfo(const JSONRPCRequest& request)
{
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getmemoryinfo (\"mode\")\n"
            "Returns an object containing information about memory usage.\n"
            "Arguments:\n"
            "1. \"mode\" determines what kind of information is returned. This argument is optional, the default mode is \"stats\".\n"
            "  - \"stats\" returns general statistics about memory usage in the daemon.\n"
            "  - \"mallocinfo\" returns an XML string describing low-level heap state (only available if compiled with glibc 2.10+).\n"
            "\nResult (mode \"stats\"):\n"
            "{\n"
            "  \"locked\": {               (json object) Information about locked memory manager\n"
            "    \"used\": xxxxx,          (numeric) Number of bytes used\n"
            "    \"free\": xxxxx,          (numeric) Number of bytes available in current arenas\n"
            "    \"total\": xxxxxxx,       (numeric) Total number of bytes managed\n"
            "    \"locked\": xxxxxx,       (numeric) Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk.\n"
            "    \"chunks_used\": xxxxx,   (numeric) Number allocated chunks\n"
            "    \"chunks_free\": xxxxx,   (numeric) Number unused chunks\n"
            "  }\n"
            "}\n"
            "\nResult (mode \"mallocinfo\"):\n"
            "\"<malloc version=\"1\">...\"\n"
            "\nExamples:\n"
            + HelpExampleCli("getmemoryinfo", "")
            + HelpExampleRpc("getmemoryinfo", "")
        );

    std::string mode = request.params[0].isNull() ? "stats" : request.params[0].get_str();
    if (mode == "stats") {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("locked", RPCLockedMemoryInfo()));
        return obj;
    } else if (mode == "mallocinfo") {
#ifdef HAVE_MALLOC_INFO
        return RPCMallocInfo();
#else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "mallocinfo is only available when compiled with glibc 2.10+");
#endif
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown mode " + mode);
    }
}

uint32_t getCategoryMask(UniValue cats) {
    cats = cats.get_array();
    uint32_t mask = 0;
    for (unsigned int i = 0; i < cats.size(); ++i) {
        uint32_t flag = 0;
        std::string cat = cats[i].get_str();
        if (!GetLogCategory(&flag, &cat)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown logging category " + cat);
        }
        mask |= flag;
    }
    return mask;
}

UniValue logging(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "logging [include,...] <exclude>\n"
            "Gets and sets the logging configuration.\n"
            "When called without an argument, returns the list of categories that are currently being debug logged.\n"
            "When called with arguments, adds or removes categories from debug logging.\n"
            "The valid logging categories are: " + ListLogCategories() + "\n"
            "libevent logging is configured on startup and cannot be modified by this RPC during runtime."
            "Arguments:\n"
            "1. \"include\" (array of strings) add debug logging for these categories.\n"
            "2. \"exclude\" (array of strings) remove debug logging for these categories.\n"
            "\nResult: <categories>  (string): a list of the logging categories that are active.\n"
            "\nExamples:\n"
            + HelpExampleCli("logging", "\"[\\\"all\\\"]\" \"[\\\"http\\\"]\"")
            + HelpExampleRpc("logging", "[\"all\"], \"[libevent]\"")
        );
    }

    uint32_t originalLogCategories = logCategories;
    if (request.params[0].isArray()) {
        logCategories |= getCategoryMask(request.params[0]);
    }

    if (request.params[1].isArray()) {
        logCategories &= ~getCategoryMask(request.params[1]);
    }

    // Update libevent logging if BCLog::LIBEVENT has changed.
    // If the library version doesn't allow it, UpdateHTTPServerLogging() returns false,
    // in which case we should clear the BCLog::LIBEVENT flag.
    // Throw an error if the user has explicitly asked to change only the libevent
    // flag and it failed.
    uint32_t changedLogCategories = originalLogCategories ^ logCategories;
    if (changedLogCategories & BCLog::LIBEVENT) {
        if (!UpdateHTTPServerLogging(logCategories & BCLog::LIBEVENT)) {
            logCategories &= ~BCLog::LIBEVENT;
            if (changedLogCategories == BCLog::LIBEVENT) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "libevent logging cannot be updated when using libevent before v2.1.1.");
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    std::vector<CLogCategoryActive> vLogCatActive = ListActiveLogCategories();
    for (const auto& logCatActive : vLogCatActive) {
        result.pushKV(logCatActive.category, logCatActive.active);
    }

    return result;
}

UniValue echo(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "echo|echojson \"message\" ...\n"
            "\nSimply echo back the input arguments. This command is for testing.\n"
            "\nThe difference between echo and echojson is that echojson has argument conversion enabled in the client-side table in"
            "merit-cli and the GUI. There is no server-side difference."
        );

    return request.params;
}

bool getAddressFromIndex(const int &type, const uint160 &hash, std::string &address)
{
    if (type == 1) {
        address = EncodeDestination(CKeyID(hash));
    } else if (type == 2) {
        address = EncodeDestination(CScriptID(hash));
    } else if (type == 3) {
        address = EncodeDestination(CParamScriptID(hash));
    } else {
        return false;
    }
    return true;
}

bool getAddressesFromParams(const UniValue& params, std::vector<AddressPair> &addresses)
{
    if (params[0].isStr()) {
        auto stringAddress = params[0].get_str();
        CMeritAddress address(stringAddress);
        uint160 hashBytes;
        int type = 0;
        if (!address.GetIndexKey(hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address: " + stringAddress);
        }
        addresses.push_back(std::make_pair(hashBytes, type));
    } else if (params[0].isObject()) {
        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Addresses is expected to be an array");
        }

        std::vector<UniValue> values = addressValues.getValues();

        for (std::vector<UniValue>::iterator it = values.begin(); it != values.end(); ++it) {
            auto stringAddress = it->get_str();
            CMeritAddress address(stringAddress);
            uint160 hashBytes;
            int type = 0;
            if (!address.GetIndexKey(hashBytes, type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address: " + stringAddress);
            }
            addresses.push_back(std::make_pair(hashBytes, type));
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address, must be a string or an object with key 'addresses'");
    }

    return true;
}

bool heightSort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b) {
    return a.second.blockHeight < b.second.blockHeight;
}

bool timestampSort(std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> a,
                   std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> b) {
    return a.second.time < b.second.time;
}

UniValue getaddressmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressmempool\n"
            "\nReturns all mempool deltas for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\"  (string) The base58check encoded address\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"outputIndex\"  (number) The related input or output index\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"timestamp\"  (number) The time the transaction entered the mempool (seconds)\n"
            "    \"prevtxid\"  (string) The previous txid (if spending)\n"
            "    \"prevout\"  (string) The previous transaction output index (if spending)\n"
            "    \"isInvite\"  (boolean) If transaction is an invite\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressmempool", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> > indexes;

    if (!mempool.getAddressIndex(addresses, indexes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
    }

    std::sort(indexes.begin(), indexes.end(), timestampSort);

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> >::iterator it = indexes.begin();
         it != indexes.end(); it++) {

        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.addressBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.push_back(Pair("address", address));
        delta.push_back(Pair("txid", it->first.txhash.GetHex()));
        delta.push_back(Pair("outputIndex", (int)it->first.index));
        delta.push_back(Pair("satoshis", it->second.amount));
        delta.push_back(Pair("script", HexStr(it->second.scriptPubKey)));
        delta.push_back(Pair("timestamp", it->second.time));
        delta.push_back(Pair("isInvite", it->first.invite));
        if (it->second.amount < 0) {
            delta.push_back(Pair("prevtxid", it->second.prevhash.GetHex()));
            delta.push_back(Pair("prevout", (int)it->second.prevout));
        }
        result.push_back(delta);
    }

    return result;
}

UniValue processMempoolReferral(const referral::ReferralTxMemPool::RefIter entryit, const AddressPair& address)
{
    const auto referral = entryit->GetSharedEntryValue();

    UniValue delta(UniValue::VOBJ);

    delta.push_back(Pair("refid", referral->GetHash().GetHex()));
    delta.push_back(Pair("address", CMeritAddress{referral->addressType, referral->GetAddress()}.ToString()));

    const auto cached_parent_referral = prefviewdb->GetReferral(referral->parentAddress);
    if (cached_parent_referral) {
        delta.push_back(Pair("inviterrefid", cached_parent_referral->GetHash().GetHex()));
    } else {
        const auto parent_referral_entry_it = mempoolReferral.Get(referral->parentAddress);
        if (parent_referral_entry_it) {
            delta.push_back(Pair("inviterrefid", parent_referral_entry_it->GetHash().GetHex()));
        }
    }
    delta.push_back(Pair("timestamp", entryit->GetTime()));
    delta.push_back(Pair("raw", EncodeHexRef(*referral)));

    return delta;
}

UniValue getaddressmempoolreferrals(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressmempoolreferrals\n"
            "\nReturns all mempool referrals for an address.\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\"        (string) The base58check encoded address\n"
            "    \"refid\"          (string) The related txid\n"
            "    \"inviterrefid\"    (string) inviter referral id\n"
            "    \"timestamp\"      (number) The time the referral entered the mempool (seconds)\n"
            "    \"raw\"            (string) Raw encoded referral object\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressmempoolreferrals", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressmempoolreferrals", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UniValue result(UniValue::VARR);

    std::set<referral::ReferralRef> referrals;
    for (const auto& address: addresses) {
        const auto entryit = mempoolReferral.mapRTx.get<referral::referral_address>().find(address.first);

        if (entryit != mempoolReferral.mapRTx.get<referral::referral_address>().end()) {
            auto entryit_ = mempoolReferral.mapRTx.project<0>(entryit);
            result.push_back(processMempoolReferral(entryit_, address));
        }

        // look for referrals that have provided address as a parentAddress
        auto it = mempoolReferral.Find(address.first);
        while (it.first != it.second) {
            result.push_back(processMempoolReferral(mempoolReferral.mapRTx.project<0>(it.first), address));

            it.first++;
        }
    }

    return result;
}

UniValue getaddressutxos(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressutxos\n"
            "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ],\n"
            "  \"invites\"    (boolean) Weather to send invites utxos instead general txs\n"
            "  \"chainInfo\"  (boolean) Include chain info with results\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {\n"
            "    \"address\"  (string) The address base58check encoded\n"
            "    \"txid\"  (string) The output txid\n"
            "    \"height\"  (number) The block height\n"
            "    \"outputIndex\"  (number) The output index\n"
            "    \"script\"  (strin) The script hex encoded\n"
            "    \"satoshis\"  (number) The number of satoshis of the output\n"
            "    \"isCoinbase\"  (boolean) If transaction is a coinbase\n"
            "    \"isInvite\"  (boolean) If transaction is an invite\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
            );

    bool includeChainInfo = false;
    bool request_invites = false;
    if (request.params[0].isObject()) {
        UniValue chainInfo = find_value(request.params[0].get_obj(), "chainInfo");
        if (chainInfo.isBool()) {
            includeChainInfo = chainInfo.get_bool();
        }

        UniValue invites = find_value(request.params[0].get_obj(), "invites");
        if (invites.isBool()) {
            request_invites = invites.get_bool();
        }
    }

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    for (std::vector<AddressPair>::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressUnspent((*it).first, (*it).second, request_invites, unspentOutputs)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    UniValue utxos(UniValue::VARR);
    utxos.reserve(unspentOutputs.size());

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        output.push_back(Pair("address", address));
        output.push_back(Pair("txid", it->first.txhash.GetHex()));
        output.push_back(Pair("outputIndex", (int)it->first.index));
        output.push_back(Pair("script", HexStr(it->second.script)));
        output.push_back(Pair("satoshis", it->second.satoshis));
        output.push_back(Pair("height", it->second.blockHeight));
        output.push_back(Pair("isCoinbase", it->first.isCoinbase));
        output.push_back(Pair("isInvite", it->first.isInvite));
        utxos.push_back(output);
    }

    if (includeChainInfo) {
        UniValue result(UniValue::VOBJ);
        result.push_back(Pair("utxos", utxos));

        LOCK(cs_main);
        result.push_back(Pair("hash", chainActive.Tip()->GetBlockHash().GetHex()));
        result.push_back(Pair("height", (int)chainActive.Height()));
        return result;
    } else {
        return utxos;
    }
}

UniValue getaddressdeltas(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1 || !request.params[0].isObject())
        throw std::runtime_error(
            "getaddressdeltas\n"
            "\nReturns all changes for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "  \"start\" (number) The start block height\n"
            "  \"end\" (number) The end block height\n"
            "  \"chainInfo\" (boolean) Include chain info in results, only applies if start and end specified\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"index\"  (number) The related input or output index\n"
            "    \"height\"  (number) The block height\n"
            "    \"address\"  (string) The base58check encoded address\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );


    UniValue startValue = find_value(request.params[0].get_obj(), "start");
    UniValue endValue = find_value(request.params[0].get_obj(), "end");

    UniValue chainInfo = find_value(request.params[0].get_obj(), "chainInfo");
    bool includeChainInfo = false;
    if (chainInfo.isBool()) {
        includeChainInfo = chainInfo.get_bool();
    }

    int start = 0;
    int end = 0;

    if (startValue.isNum() && endValue.isNum()) {
        start = startValue.get_int();
        end = endValue.get_int();
        if (start <= 0 || end <= 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start and end is expected to be greater than zero");
        }
        if (end < start) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "End value is expected to be greater than start");
        }
    }

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<AddressPair>::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex((*it).first, (*it).second, false, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex((*it).first, (*it).second, false, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    UniValue deltas(UniValue::VARR);

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.push_back(Pair("satoshis", it->second));
        delta.push_back(Pair("txid", it->first.txhash.GetHex()));
        delta.push_back(Pair("index", (int)it->first.index));
        delta.push_back(Pair("blockindex", (int)it->first.txindex));
        delta.push_back(Pair("height", it->first.blockHeight));
        delta.push_back(Pair("address", address));
        deltas.push_back(delta);
    }

    UniValue result(UniValue::VOBJ);

    if (includeChainInfo && start > 0 && end > 0) {
        LOCK(cs_main);

        if (start > chainActive.Height() || end > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");
        }

        CBlockIndex* startIndex = chainActive[start];
        CBlockIndex* endIndex = chainActive[end];

        UniValue startInfo(UniValue::VOBJ);
        UniValue endInfo(UniValue::VOBJ);

        startInfo.push_back(Pair("hash", startIndex->GetBlockHash().GetHex()));
        startInfo.push_back(Pair("height", start));

        endInfo.push_back(Pair("hash", endIndex->GetBlockHash().GetHex()));
        endInfo.push_back(Pair("height", end));

        result.push_back(Pair("deltas", deltas));
        result.push_back(Pair("start", startInfo));
        result.push_back(Pair("end", endInfo));

        return result;
    } else {
        return deltas;
    }
}

UniValue getaddressbalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressbalance\n"
            "\nReturns the balance for an address(es) (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\nResult:\n"
            "{\n"
            "  \"balance\"  (string) The current balance in satoshis\n"
            "  \"received\"  (string) The total number of satoshis received (including change)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressbalance", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressbalance", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<AddressPair>::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressIndex((*it).first, (*it).second, false, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    CAmount balance = 0;
    CAmount received = 0;

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        if (it->second > 0) {
            received += it->second;
        }
        balance += it->second;
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("balance", balance));
    result.push_back(Pair("received", received));

    return result;
}

namespace {
    // (height, invite, id)
    using AddressTx = std::tuple<int, bool, std::string>;
}

struct TxHeightCmp {
    bool operator() (const AddressTx& lhs, const AddressTx& rhs) const {
        // elements are equal
        if (lhs == rhs) {
            return false;
        }

        // if height is the same check if invite or not - invites go first
        if (get<0>(lhs) == get<0>(rhs)) {
            if (get<1>(lhs) && !get<1>(rhs)) {
                return true;
            } else {
                return get<2>(lhs) <= get<2>(rhs);
            }
        }

        // tx with less height goes first
        return get<0>(lhs) < get<0>(rhs);
    }
};

UniValue getaddresstxids(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddresstxids\n"
            "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "  \"start\" (number) The start block height\n"
            "  \"end\" (number) The end block height\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  \"transactionid\"  (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    int start = 0;
    int end = 0;
    if (request.params[0].isObject()) {
        UniValue startValue = find_value(request.params[0].get_obj(), "start");
        UniValue endValue = find_value(request.params[0].get_obj(), "end");
        if (startValue.isNum() && endValue.isNum()) {
            start = startValue.get_int();
            end = endValue.get_int();
        }
    }

    std::set<AddressTx, TxHeightCmp> txids;

    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;

    for (const auto& it : addresses) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex(it.first, it.second, true, addressIndex, start, end) ||
                !GetAddressIndex(it.first, it.second, false, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex(it.first, it.second, true, addressIndex) ||
                !GetAddressIndex(it.first, it.second, false, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    for (const auto& it: addressIndex) {
        int height = it.first.blockHeight;
        std::string txid = it.first.txhash.GetHex();

        txids.insert(std::make_tuple(height, it.first.invite, txid));
    }

    UniValue result(UniValue::VARR);

    for (const auto& it: txids) {
        result.push_back(get<2>(it));
    }

    return result;

}

UniValue getaddressreferrals(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getaddressreferrals\n"
            "\nReturns referrals for an address(es) (requires referralindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ]\n"
            "}\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"refid\"          (string) The related txid\n"
            "    \"raw\"            (string) Raw encoded referral object\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressreferrals", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            + HelpExampleRpc("getaddressreferrals", "{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}")
        );
    }

    assert(prefviewcache);
    assert(prefviewdb);

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UniValue result(UniValue::VARR);

    for (const auto& address: addresses) {
        const auto referral = prefviewcache->GetReferral(address.first);
        const auto children = prefviewdb->GetChildren(address.first);

        if (!referral) {
            continue;
        }

        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("refid", referral->GetHash().GetHex()));
        item.push_back(Pair("raw", EncodeHexRef(*referral)));
        result.push_back(item);

        for (const auto& child_address: children) {
            const auto child_referral = prefviewcache->GetReferral(child_address);

            if (child_referral) {
                UniValue item(UniValue::VOBJ);
                item.push_back(Pair("refid", child_referral->GetHash().GetHex()));
                item.push_back(Pair("raw", EncodeHexRef(*child_referral)));
                result.push_back(item);
            }
        }
    }

    return result;
}

UniValue getspentinfo(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 1 || !request.params[0].isObject())
        throw std::runtime_error(
            "getspentinfo\n"
            "\nReturns the txid and index where an output is spent.\n"
            "\nArguments:\n"
            "{\n"
            "  \"txid\" (string) The hex string of the txid\n"
            "  \"index\" (number) The start block height\n"
            "}\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\"  (string) The transaction id\n"
            "  \"index\"  (number) The spending input index\n"
            "  ,...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getspentinfo", "'{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}'")
            + HelpExampleRpc("getspentinfo", "{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}")
        );

    UniValue txidValue = find_value(request.params[0].get_obj(), "txid");
    UniValue indexValue = find_value(request.params[0].get_obj(), "index");

    if (!txidValue.isStr() || !indexValue.isNum()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid or index");
    }

    uint256 txid = ParseHashV(txidValue, "txid");
    int outputIndex = indexValue.get_int();

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    if (!GetSpentIndex(key, value)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("txid", value.txid.GetHex()));
    obj.push_back(Pair("index", (int)value.inputIndex));
    obj.push_back(Pair("height", value.blockHeight));

    return obj;
}

CAmount GetAmount(const CMempoolAddressDelta& v) { return v.amount; }
CAmount GetAmount(CAmount amount) { return amount; }

template <class IndexPair>
void DecorateEasySendTransactionInformation(UniValue& ret, const IndexPair& pair, const size_t& confirmations)
{
    const auto& key = pair.first;

    ret.push_back(Pair("txid", key.txhash.GetHex()));
    ret.push_back(Pair("index", static_cast<int>(key.index)));
    ret.push_back(Pair("amount", !key.invite ? ValueFromAmount(GetAmount(pair.second)) : GetAmount(pair.second)));
    ret.push_back(Pair("spending", key.spending));
    ret.push_back(Pair("confirmations", static_cast<int>(confirmations)));
    ret.push_back(Pair("invite", key.invite));

    CSpentIndexValue spent_value;
    bool spent = false;
    if(confirmations == 0) {
        spent = mempool.getSpentIndex(
                {key.txhash, static_cast<unsigned int>(key.index)},
                spent_value);
    } else {
        spent = GetSpentIndex(
                {key.txhash, static_cast<unsigned int>(key.index)},
                spent_value);
    }

    if(spent) {
        ret.push_back(Pair("spenttxid", spent_value.txid.GetHex()));
        ret.push_back(Pair("spentindex", static_cast<int>(spent_value.inputIndex)));
    }
    ret.push_back(Pair("spent", spent));
}

UniValue getinputforeasysend(const JSONRPCRequest& request)
{
    const int SCRIPT_TYPE = 2;

    if (request.fHelp || request.params.size() != 1 || !request.params[0].isStr())
        throw std::runtime_error(
                "getinputforeasysend scriptaddress\n"
                "\nReturns the txid and index where an output is spent.\n"
                "\nArguments:\n"
                "\"scriptaddress\" (string) Base58 address of script used in easy transaction.\n"
                "}\n"
                "\nResult:\n"
                "[\n"
                "   {\n"
                "       \"found\"  (bool) True if found otherwise false\n"
                "       \"txid\"  (string) The transaction id\n"
                "       \"index\"  (number) The spending input index\n"
                "       ,...\n"
                "   }\n"
                "]\n"
                "\nExamples:\n"
                + HelpExampleCli("getinputforeasysend", "mp2FqA5kiszSWREEQXBmmMmGBYwiLuGFLt")
                );

    auto script_address = request.params[0].get_str();

    auto dest = LookupDestination(script_address);
    auto script_id = boost::get<CScriptID>(&dest);
    if(script_id == nullptr) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid scriptaddress");
    }

    std::vector<AddressPair> addresses = {{*script_id, 2}};
    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> > mempool_indexes;

    mempool.getAddressIndex(addresses, mempool_indexes);

    UniValue ret(UniValue::VARR);
    if(!mempool_indexes.empty()) {
        for (const auto& index: mempool_indexes) {
            UniValue in(UniValue::VOBJ);

            DecorateEasySendTransactionInformation(in, index, 0);
            ret.push_back(in);
        }

        return ret;
    } else {
        std::vector<std::pair<CAddressIndexKey, CAmount>> coins;
        GetAddressIndex(*script_id, SCRIPT_TYPE, false, coins);
        GetAddressIndex(*script_id, SCRIPT_TYPE, true, coins);

        if (!coins.empty()) {
            for (const auto& coin : coins) {
                UniValue in(UniValue::VOBJ);

                size_t confirmations = std::max(0, chainActive.Height() - coin.first.blockHeight);
                DecorateEasySendTransactionInformation(in, coin, confirmations);
                ret.push_back(in);
            }

            return ret;
        }
    }

    return ret;
}

UniValue getaddressrewards(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressrewards\n"
            "\nReturns rewards for an address (requires addressindex to be enabled).\n"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ],\n"
            "}\n"
            "\nResult\n"
            "[\n"
            "  {\n"
            "    \"address\"  (string) The address base58check encoded\n"
            "    \"rewards\": "
            "       {\n"
            "           \"mining\": x.xxxx,     (numeric) The total amount in " + CURRENCY_UNIT + " received for this account for mining.\n"
            "           \"ambassador\": x.xxxx, (numeric) The total amount in " + CURRENCY_UNIT + " received for this account for being ambassador.\n"
            "       }\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressrewards", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
            );

    std::vector<AddressPair> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UniValue ret(UniValue::VARR);

    for (const auto& addrit : addresses) {
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;

        if (!GetAddressUnspent(addrit.first, addrit.second, false, unspentOutputs)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }

        UniValue output(UniValue::VOBJ);

        const pog::RewardsAmount rewards = std::accumulate(unspentOutputs.begin(), unspentOutputs.end(), pog::RewardsAmount{},
            [](pog::RewardsAmount& acc, const std::pair<CAddressUnspentKey, CAddressUnspentValue>& it) {
                const auto& key = it.first;
                const auto& value = it.second;

                if (key.isCoinbase) {
                    if (key.index == 0) {
                        acc.mining += value.satoshis;
                    } else {
                        acc.ambassador += value.satoshis;
                    }
                }

                return acc;
            });

        std::string address;
        if (!getAddressFromIndex(addrit.second, addrit.first, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue rewardsOutput(UniValue::VOBJ);

        rewardsOutput.push_back(Pair("mining", rewards.mining));
        rewardsOutput.push_back(Pair("ambassador", rewards.ambassador));

        output.push_back(Pair("address", address));
        output.push_back(Pair("rewards", rewardsOutput));

        ret.push_back(output);
    }

    return ret;
}

UniValue getaddressanv(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getaddressanv"
            "\nArguments:\n"
            "{\n"
            "  \"addresses\"\n"
            "    [\n"
            "      \"address\"  (string) The base58check encoded address\n"
            "      ,...\n"
            "    ],\n"
            "}\n"
            "\nReturns ANV for all addresess input.\n"
            "\nResult:\n"
            "ANV              (numeric) The total Aggregate Network Value in " + CURRENCY_UNIT + " received for the keys or wallet.\n"
            + HelpExampleCli("getaddressanv", "'{\"addresses\": [\"12c6DSiU4Rq3P4ZxziKxzrL5LmMBrzjrJX\"]}'")
        );
    }

    assert(prefviewdb);

    ObserveSafeMode();

    auto params = request.params[0].get_obj();
    std::vector<AddressPair> addresses;
    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<referral::Address> keys;
    keys.reserve(addresses.size());

    for(const AddressPair &addressPair: addresses) {
        auto key = addressPair.first;
        keys.push_back(key);
    }

    auto anvs = pog::GetANVs(keys, *prefviewdb);

    auto total =
        std::accumulate(std::begin(anvs), std::end(anvs), CAmount{0},
            [](CAmount total, const referral::AddressANV& v){
                return total + v.anv;
            });

    return total;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "control",            "getinfo",                &getinfo,                {} }, /* uses wallet if enabled */
    { "control",            "getmemoryinfo",          &getmemoryinfo,          {"mode"} },
    { "util",               "validateaddress",        &validateaddress,        {"address"} }, /* uses wallet if enabled */
    { "util",               "validatealias",          &validatealias,          {"alias"} },
    { "util",               "createmultisig",         &createmultisig,         {"nrequired","keys"} },
    { "util",               "verifymessage",          &verifymessage,          {"address","signature","message"} },
    { "util",               "signdata",               &signdata,               {"data","key"} },
    { "util",               "verifydata",             &verifydata,             {"data","signature","pubkey"} },
    { "util",               "signmessagewithprivkey", &signmessagewithprivkey, {"privkey","message"} },

    /* Address index */
    { "addressindex",       "getaddressmempool",            &getaddressmempool,          {} },
    { "addressindex",       "getaddressmempoolreferrals",   &getaddressmempoolreferrals, {} },
    { "addressindex",       "getaddressutxos",              &getaddressutxos,            {} },
    { "addressindex",       "getaddressdeltas",             &getaddressdeltas,           {} },
    { "addressindex",       "getaddresstxids",              &getaddresstxids,            {} },
    { "addressindex",       "getaddressreferrals",          &getaddressreferrals,        {} },
    { "addressindex",       "getaddressbalance",            &getaddressbalance,          {} },
    { "addressindex",       "getaddressrewards",            &getaddressrewards,          {} },
    { "addressindex",       "getaddressanv",                &getaddressanv,              {} },

    /* Blockchain */
    { "blockchain",         "getspentinfo",           &getspentinfo,           {} },
    { "blockchain",         "getinputforeasysend",    &getinputforeasysend,           {"scriptaddress"} },


    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            {"timestamp"}},
    { "hidden",             "echo",                   &echo,                   {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "echojson",               &echo,                   {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "logging",                &logging,                {"include", "exclude"}},
};

void RegisterMiscRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

