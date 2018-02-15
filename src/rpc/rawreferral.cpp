// Copyright (c) 2015-2017 The Merit Foundation developers
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/validation.h"
#include "core_io.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/referral.h"
#include "referrals.h"
#include "refmempool.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "uint256.h"
#include "validation.h"

#include <univalue.h>

using namespace referral;

void RefToJSON(const Referral& ref, const uint256 hashBlock, UniValue& entry)
{
    RefToUniv(ref, uint256(), entry, true, RPCSerializationFlags());

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.push_back(Pair("height", pindex->nHeight));
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->nHeight));
                entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
            } else {
                entry.push_back(Pair("height", -1));
                entry.push_back(Pair("confirmations", 0));
            }
        }
    }
}

UniValue getrawreferral(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getrawreferral \"refid\" ( verbose )\n"

            "\nNOTE: By default this function only works for mempool referrals.\n"
            "If the -referralindex option is enabled, it also works for blockchain referrals.\n"

            "\nReturn the raw referral data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'refid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'refid'.\n"

            "\nArguments:\n"
            "1. \"refid\"     (string, required) Referral id\n"
            "2. verbose       (bool, optional, default=false) If false, return a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'refid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",          (string) The serialized, hex-encoded data for 'refid'\n"
            "  \"refid\" : \"id\",          (string) Referral id - hash (same as provided), address or alias\n"
            "  \"size\" : n,                (numeric) The serialized referral size\n"
            "  \"vsize\" : n,               (numeric) The virtual referral size\n"
            "  \"version\" : n,             (numeric) The version\n"
            "  \"address\" : \"xxx\",       (string) Beaconed address\n"
            "  \"parentAddress\" : \"xxx\", (string) Parent address, that was used to unlock this referral\n"
            "  \"alias\" : \"xxx\",         (string, optional) Address alias\n"
            "  \"pubkey\" : \"xxx\",        (string) Signer pubkey\n"
            "  \"signature\" : \"xxx\",     (string) Referral signature\n"
            "  \"blockhash\" : \"hash\",    (string) Block hash\n"
            "  \"height\" : n,              (numeric) Block height\n"
            "  \"confirmations\" : n,       (numeric) Confirmations count\n"
            "  \"blocktime\" : ttt          (numeric) Block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getrawreferral", "\"myrefid\"") +
            HelpExampleCli("getrawreferral", "\"myrefid\" true") +
            HelpExampleCli("getrawreferral", "\"myrefid\" 1") +
            HelpExampleRpc("getrawreferral", "\"myrefid\", true"));

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        if (request.params[1].isNum()) {
            if (request.params[1].get_int() != 0) {
                fVerbose = true;
            }
        } else if (request.params[1].isBool()) {
            if (request.params[1].isTrue()) {
                fVerbose = true;
            }
        } else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid type provided. Verbose parameter must be a boolean.");
        }
    }

    ReferralId referral_id;

    try {
        referral_id = ParseHashV(request.params[0], "refid");
    } catch (const UniValue& e) {
        auto address_or_alias = request.params[0];
        if (!address_or_alias.isStr()) {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid type provided. refid should be a string.");
        }

        auto dest = LookupDestination(address_or_alias.get_str());
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_TYPE_ERROR, "Provided refid is not a valid referral address or known alias.");
        }

        auto address = CMeritAddress{dest};
        referral_id = *(address.GetUint160());
    }

    ReferralRef ref;
    ref = LookupReferral(referral_id);

    if (!ref) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about referral");
    }

    auto hash = ref->GetHash();

    uint256 hashBlock;
    {
        LOCK(cs_main);

        if (!GetReferral(hash, ref, hashBlock)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about referral");
        }
    }

    if (!fVerbose) {
        return EncodeHexRef(*ref);
    }

    UniValue result(UniValue::VOBJ);
    RefToJSON(*ref, hashBlock, result);

    return result;
}

UniValue sendrawreferral(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawreferral \"hexstring\"\n"
            "\nSubmits raw referral (serialized, hex-encoded) to local node and network.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw referral)\n"
            "\nResult:\n"
            "\"hex\"             (string) The referral hash in hex\n"
            "\nExamples:\n"
            "\nSend the referral (signed hex)\n");

    ObserveSafeMode();
    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    MutableReferral mref;
    if (!DecodeHexRef(mref, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Referral decode failed");
    }

    ReferralRef ref(MakeReferralRef(std::move(mref)));
    const uint256& hashRef = ref->GetHash();

    // push to local node mempool
    CValidationState state;
    bool fMissingReferrer;

    if (!AcceptReferralToMemoryPool(mempoolReferral, state, ref, fMissingReferrer, false)) {
        if (state.IsInvalid()) {
            throw JSONRPCError(RPC_REFERRAL_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else {
            if (fMissingReferrer) {
                throw JSONRPCError(RPC_REFERRAL_ERROR, "Missing referrer");
            }
            throw JSONRPCError(RPC_REFERRAL_ERROR, state.GetRejectReason());
        }
    }

    if (!g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    CInv inv(MSG_REFERRAL, hashRef);
    g_connman->ForEachNode([&inv](CNode* pnode) {
        pnode->PushInventory(inv);
    });

    return hashRef.GetHex();
}

UniValue decoderawreferral(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawreferral \"hexstring\"\n"
            "\nReturn a JSON object representing unserialized referral.\n"

            "\nArguments:\n"
            "1. \"hexstring\"  (string, required) Serialized referral hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"refid\": \"id\",           (string) Referral hash\n"
            "  \"version\": n,            (numeric) Referral version\n"
            "  \"address\": \"xxx\",        (string) Beaconed address\n"
            "  \"alias\": \"xxx\",          (string) Address alias\n"
            "  \"parentAddress\": \"xxx\",  (string) Unlock address\n"
            "  \"size\": n,               (numeric) Referral size\n"
            "  \"vsize\": n,              (numeric) Virtual referral size\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawreferral", "\"hexstring\"")
            + HelpExampleRpc("decoderawreferral", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR});

    referral::MutableReferral ref;

    if (!DecodeHexRef(ref, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Referral decode failed");

    UniValue result(UniValue::VOBJ);
    RefToUniv(Referral(std::move(ref)), uint256(), result, false);

    return result;
}

static const CRPCCommand commands[] =
    {
        //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  -----------------------  ----------
        {"rawreferral", "getrawreferral", &getrawreferral, {"refid", "verbose"}},
        {"rawreferral", "sendrawreferral", &sendrawreferral, {"hexstring"}},
        {"rawreferral", "decoderawreferral", &decoderawreferral, {"hexstring"}},
};

void RegisterRawReferralRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
