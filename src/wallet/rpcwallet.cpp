// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "miner.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "referrals.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "rpc/misc.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/vault.h"
#include "pog/anv.h"

#include <init.h>  // For StartShutdown

#include <stdint.h>
#include <univalue.h>
#include <numeric>
#include <sstream>

static const std::string WALLET_ENDPOINT_BASE = "/wallet/";

namespace
{
    const size_t RANDOM_BYTES_SIZE = 16;
    const bool COMPRESSED_KEY = true;
}

CWallet *GetWalletForJSONRPCRequest(const JSONRPCRequest& request)
{
    if (request.URI.substr(0, WALLET_ENDPOINT_BASE.size()) == WALLET_ENDPOINT_BASE) {
        // wallet endpoint was used
        std::string requestedWallet = urlDecode(request.URI.substr(WALLET_ENDPOINT_BASE.size()));
        for (CWalletRef pwallet : ::vpwallets) {
            if (pwallet->GetName() == requestedWallet) {
                return pwallet;
            }
        }
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
    }
    return ::vpwallets.size() == 1 || (request.fHelp && ::vpwallets.size() > 0) ? ::vpwallets[0] : nullptr;
}

std::string HelpRequiringPassphrase(CWallet * const pwallet)
{
    return pwallet && pwallet->IsCrypted()
        ? "\nRequires wallet passphrase to be set with walletpassphrase call."
        : "";
}

bool EnsureWalletIsAvailable(CWallet * const pwallet, bool avoidException)
{
    if (pwallet) return true;
    if (avoidException) return false;
    if (::vpwallets.empty()) {
        // Note: It isn't currently possible to trigger this error because
        // wallet RPC methods aren't registered unless a wallet is loaded. But
        // this error is being kept as a precaution, because it's possible in
        // the future that wallet RPC methods might get or remain registered
        // when no wallets are loaded.
        throw JSONRPCError(
            RPC_METHOD_NOT_FOUND, "Method not found (wallet method is disabled because no wallet is loaded)");
    }
    throw JSONRPCError(RPC_WALLET_NOT_SPECIFIED,
        "Wallet file not specified (must request wallet RPC through /wallet/<filename> uri-path).");
}

void EnsureWalletIsUnlocked(CWallet * const pwallet)
{
    if (pwallet->IsLocked()) {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    }

    if (!pwallet->IsReferred()) {
        throw JSONRPCError(RPC_WALLET_NOT_REFERRED, "Error: Wallet is not beaconed. Use referral code to beacon first.");
    }
}

void WalletTxToJSON(const CWalletTx& wtx, UniValue& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    entry.push_back(Pair("confirmations", confirms));
    if (wtx.IsCoinBase())
        entry.push_back(Pair("generated", true));
    if (confirms > 0)
    {
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.nIndex));
        entry.push_back(Pair("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime()));
    } else {
        entry.push_back(Pair("trusted", wtx.IsTrusted()));
    }
    uint256 hash = wtx.GetHash();
    entry.push_back(Pair("txid", hash.GetHex()));
    UniValue conflicts(UniValue::VARR);
    for (const uint256& conflict : wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.push_back(Pair("walletconflicts", conflicts));
    entry.push_back(Pair("time", wtx.GetTxTime()));
    entry.push_back(Pair("timereceived", (int64_t)wtx.nTimeReceived));

    // Add opt-in RBF status
    std::string rbfStatus = "no";
    if (confirms <= 0) {
        LOCK(mempool.cs);
        RBFTransactionState rbfState = IsRBFOptIn(wtx, mempool);
        if (rbfState == RBF_TRANSACTIONSTATE_UNKNOWN)
            rbfStatus = "unknown";
        else if (rbfState == RBF_TRANSACTIONSTATE_REPLACEABLE_BIP125)
            rbfStatus = "yes";
    }
    entry.push_back(Pair("bip125-replaceable", rbfStatus));

    for (const std::pair<std::string, std::string>& item : wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));
}

std::string AccountFromValue(const UniValue& value)
{
    std::string strAccount = value.get_str();
    if (strAccount == "*")
        throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
    return strAccount;
}

UniValue getnewaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getnewaddress ( \"account\" )\n"
            "\nReturns a new Merit address for receiving payments.\n"
            "If 'account' is specified (DEPRECATED), it is added to the address book \n"
            "so payments received with the address will be credited to 'account'.\n"
            "\nArguments:\n"
            "1. \"account\"        (string, optional) DEPRECATED. The account name for the address to be linked to. If not provided, the default account \"\" is used. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created if there is no account by the given name.\n"
            "\nResult:\n"
            "\"address\"    (string) The new merit address\n"
            "\nExamples:\n"
            + HelpExampleCli("getnewaddress", "")
            + HelpExampleRpc("getnewaddress", "")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    std::string strAccount;
    if (!request.params[0].isNull())
        strAccount = AccountFromValue(request.params[0]);

    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }
    CKeyID keyID = newKey.GetID();

    pwallet->SetAddressBook(keyID, strAccount, "receive");

    return EncodeDestination(keyID);
}


CTxDestination GetAccountAddress(CWallet* const pwallet, std::string strAccount, bool bForceNew=false)
{
    CPubKey pubKey;
    if (!pwallet->GetAccountPubkey(pubKey, strAccount, bForceNew)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    return pubKey.GetID();
}

UniValue getaccountaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaccountaddress \"account\"\n"
            "\nDEPRECATED. Returns the current Merit address for receiving payments to this account.\n"
            "\nArguments:\n"
            "1. \"account\"       (string, required) The account name for the address. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created and a new address created  if there is no account by the given name.\n"
            "\nResult:\n"
            "\"address\"          (string) The account merit address\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccountaddress", "")
            + HelpExampleCli("getaccountaddress", "\"\"")
            + HelpExampleCli("getaccountaddress", "\"myaccount\"")
            + HelpExampleRpc("getaccountaddress", "\"myaccount\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    std::string strAccount = AccountFromValue(request.params[0]);

    UniValue ret(UniValue::VSTR);

    ret = EncodeDestination(GetAccountAddress(pwallet, strAccount));
    return ret;
}


UniValue getrawchangeaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getrawchangeaddress\n"
            "\nReturns a new Merit address, for receiving change.\n"
            "This is for use with raw transactions, NOT normal use.\n"
            "\nResult:\n"
            "\"address\"    (string) The address\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawchangeaddress", "")
            + HelpExampleRpc("getrawchangeaddress", "")
       );

    LOCK2(cs_main, pwallet->cs_wallet);

    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }

    CReserveKey reservekey(pwallet);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    return EncodeDestination(keyID);
}


UniValue setaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "setaccount \"address\" \"account\"\n"
            "\nDEPRECATED. Sets the account associated with the given address.\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The merit address to be associated with an account.\n"
            "2. \"account\"         (string, required) The account to assign the address to.\n"
            "\nExamples:\n"
            + HelpExampleCli("setaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"tabby\"")
            + HelpExampleRpc("setaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\", \"tabby\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Merit address");
    }

    std::string strAccount;
    if (!request.params[1].isNull())
        strAccount = AccountFromValue(request.params[1]);

    // Only add the account if the address is yours.
    if (IsMine(*pwallet, dest)) {
        // Detect when changing the account of an address that is the 'unused current key' of another account:
        if (pwallet->mapAddressBook.count(dest)) {
            std::string strOldAccount = pwallet->mapAddressBook[dest].name;
            if (dest == GetAccountAddress(pwallet, strOldAccount)) {
                GetAccountAddress(pwallet, strOldAccount, true);
            }
        }
        pwallet->SetAddressBook(dest, strAccount, "receive");
    }
    else
        throw JSONRPCError(RPC_MISC_ERROR, "setaccount can only be used with own address");

    return NullUniValue;
}


UniValue getaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaccount \"address\"\n"
            "\nDEPRECATED. Returns the account associated with the given address.\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The merit address for account lookup.\n"
            "\nResult:\n"
            "\"accountname\"        (string) the account address\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\"")
            + HelpExampleRpc("getaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Merit address");
    }

    std::string strAccount;
    std::map<CTxDestination, CAddressBookData>::iterator mi = pwallet->mapAddressBook.find(dest);
    if (mi != pwallet->mapAddressBook.end() && !(*mi).second.name.empty()) {
        strAccount = (*mi).second.name;
    }
    return strAccount;
}


UniValue getaddressesbyaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getaddressesbyaccount \"account\"\n"
            "\nDEPRECATED. Returns the list of addresses for the given account.\n"
            "\nArguments:\n"
            "1. \"account\"        (string, required) The account name.\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"address\"         (string) a merit address associated with the given account\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressesbyaccount", "\"tabby\"")
            + HelpExampleRpc("getaddressesbyaccount", "\"tabby\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAccount = AccountFromValue(request.params[0]);

    // Find all addresses that have the given account
    UniValue ret(UniValue::VARR);
    for (const std::pair<CTxDestination, CAddressBookData>& item : pwallet->mapAddressBook) {
        const CTxDestination& dest = item.first;
        const std::string& strName = item.second.name;
        if (strName == strAccount) {
            ret.push_back(EncodeDestination(dest));
        }
    }
    return ret;
}

static void SendMoney(
        CWallet * const pwallet,
        const CScript &scriptPubKey,
        CAmount nValue,
        bool fSubtractFeeFromAmount,
        CWalletTx& wtxNew,
        const CCoinControl& coin_control)
{
    CAmount curBalance = pwallet->GetBalance();

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    // Create and send the transaction
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired = 0;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nValue, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);
    if (!pwallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError, coin_control)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > curBalance)
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    CValidationState state;
    if (!pwallet->CommitTransaction(wtxNew, reservekey, g_connman.get(), state)) {
        strError = strprintf("Error: The transaction was rejected! Reason given: %s", state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
}

static void SendMoneyToDest(
        CWallet * const pwallet,
        const CTxDestination &address,
        CAmount nValue,
        bool fSubtractFeeFromAmount,
        CWalletTx& wtxNew,
        const CCoinControl& coin_control)
{
    // Parse Merit address
    CScript scriptPubKey = GetScriptForDestination(address);
    SendMoney(
            pwallet,
            scriptPubKey,
            nValue,
            fSubtractFeeFromAmount,
            wtxNew,
            coin_control);
}

static void ConfirmAddress(
        CWallet * const pwallet,
        const CScript &scriptPubKey,
        CWalletTx& wtxNew,
        const CCoinControl& coin_control)
{
    const int available_invites = pwallet->GetBalance(true);

    // Check amount
    if (available_invites <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No invites available");

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    // Create and send the transaction
    CReserveKey reservekey(pwallet);
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, 1, false};
    vecSend.push_back(recipient);

    if (!pwallet->CreateInviteTransaction(
                vecSend,
                wtxNew,
                reservekey,
                nChangePosRet,
                strError,
                coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(
                wtxNew,
                reservekey,
                g_connman.get(),
                state)) {
        strError = strprintf("Error: The transaction was rejected! Reason given: %s", state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
}

static UniValue EasySend(
        CWallet&  pwallet,
        CAmount value,
        const std::string& optional_password,
        const int max_blocks,
        bool fSubtractFeeFromAmount,
        CWalletTx& wtx,
        const CCoinControl& coin_control)
{
    if(max_blocks < 1 ) {
        throw JSONRPCError(RPC_PARSE_ERROR, "Error: maxblocks must be greater than 0");
    }

    CAmount balance = pwallet.GetBalance();

    // Check amount
    if (value <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");
    }

    if (value > balance) {
        throw JSONRPCError(
                RPC_WALLET_INSUFFICIENT_FUNDS,
                "Insufficient funds");
    }

    if (pwallet.GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(
                RPC_CLIENT_P2P_DISABLED,
                "Error: Peer-to-peer functionality missing or disabled");
    }

    // Reserve a key that the sender can use to cancel the transaction and retrieve
    // the funds.
    CReserveKey reserve_key(&pwallet);

    CPubKey sender_pub;
    if (!reserve_key.GetReservedKey(sender_pub)) {
        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Keypool ran out, please call keypoolrefill first");
    }

    // Create a deterministic based on the secret that was computed.
    CKey receiver_key;
    std::string secret(RANDOM_BYTES_SIZE + optional_password.size(), ' ');
    std::copy(
            std::begin(optional_password),
            std::end(optional_password),
            std::begin(secret) + RANDOM_BYTES_SIZE);

    while(!receiver_key.IsValid()) {
        GetRandBytes(reinterpret_cast<unsigned char*>(&secret[0]), RANDOM_BYTES_SIZE);
        receiver_key.MakeNewKey(std::begin(secret), std::end(secret), COMPRESSED_KEY);
    }

    auto receiver_pub = receiver_key.GetPubKey();

    // Create the easy send script to be used to store the funds
    auto easy_send_script =
        GetScriptForEasySend(max_blocks, sender_pub, receiver_pub);

    CScriptID script_id = easy_send_script;

    if(!pwallet.GenerateNewReferral(
                receiver_pub,
                pwallet.ReferralAddress(),
                "",
                receiver_key)) {

        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Unable to generate referral for receiver key");
    }

    referral::ReferralRef script_referral=
        pwallet.GenerateNewReferral(
                    script_id,
                    sender_pub.GetID(),
                    sender_pub);

    if(!script_referral) {

        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Unable to generate referral for easy send script");
    }

    CScriptID easy_send_address{script_referral->GetAddress()};
    CScript script_pub_key = GetScriptForDestination(easy_send_address);

    std::string error;
    std::vector<CRecipient> recipients = {
        {script_pub_key, value, fSubtractFeeFromAmount}
    };

    int change_pos_ret = -1;
    CAmount fee_required = 0;

    if (!pwallet.CreateTransaction(
                recipients,
                wtx,
                reserve_key,
                fee_required,
                change_pos_ret,
                error,
                coin_control)) {

        if (!fSubtractFeeFromAmount && value + fee_required > balance) {
            error = strprintf(
                    "Error: This transaction requires a transaction fee of at least %s",
                    FormatMoney(fee_required));
        }
        throw JSONRPCError(RPC_WALLET_ERROR, error);
    }

    CValidationState state;
    if (!pwallet.CommitTransaction(wtx, reserve_key, g_connman.get(), state)) {
        error = strprintf(
                "Error: The transaction was rejected! Reason given: %s",
                state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, error);
    }

    //add script to wallet so we can redeem it later if needed.
    pwallet.AddCScript(easy_send_script, easy_send_address);
    pwallet.SetAddressBook(script_id, "", "easysend");
    pwallet.SetAddressBook(easy_send_address, "", "easysend");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
    ret.push_back(Pair("secret", HexStr(secret.substr(0, RANDOM_BYTES_SIZE))));
    ret.push_back(Pair("address", EncodeDestination(easy_send_address)));
    ret.push_back(Pair("senderpubkey", HexStr(sender_pub)));
    ret.push_back(Pair("maxblocks", max_blocks));

    return ret;
}

struct EasySendCoin
{
    Coin coin;
    COutPoint out;
};

using EasySendCoins = std::vector<EasySendCoin>;


void FindEasySendCoins(const CScriptID& easy_send_address, EasySendCoins& coins)
{
    CCoinsViewCache &view_chain = *pcoinsTip;
    CCoinsViewMemPool viewMempool(&view_chain, mempool);
    CCoinsViewCache view(&viewMempool);

    const int SCRIPT_TYPE = 2;

    using MempoolOutputs = std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>;
    MempoolOutputs mempool_outputs;
    std::vector<std::pair<uint160, int> > addresses = {{easy_send_address, SCRIPT_TYPE}};
    mempool.getAddressIndex(addresses, mempool_outputs);

    for(const auto& m : mempool_outputs) {
        COutPoint out{m.first.txhash, m.first.index};
        auto coin = view.AccessCoin(out);
        if(!coin.out.IsNull()) {
            coins.push_back({coin, out});
        }
    }

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > chain_outputs;
    if(!GetAddressUnspent(easy_send_address, SCRIPT_TYPE, chain_outputs)) {
        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Cannot find coin with address: " + EncodeDestination(easy_send_address));
    }

    for(const auto& c : chain_outputs) {
        COutPoint out{c.first.txhash, c.first.index};
        auto coin = view.AccessCoin(out);
        if(!coin.out.IsNull()) {
            coins.push_back({coin, out});
        }
    }

    if(coins.empty()) {
        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Cannot find unspent coin with address: " + EncodeDestination(easy_send_address));
    }

}

void SelectEasySendCoins(
        CWallet&  pwallet,
        CCoinControl& coin_control,
        const EasySendCoins coins,
        CAmount& unspent_amount) {

    for(const auto& c : coins) {

        CSpentIndexValue spent_value;
        if(GetSpentIndex(
                    {c.out.hash, static_cast<unsigned int>(c.out.n)},
                    spent_value)) {

            continue;
        }

        unspent_amount += c.coin.out.nValue;


        //get the easy send transaction based on easy_send_address
        CTransactionRef unspent_tx;
        uint256 blockHash;
        if(!GetTransaction(
                    c.out.hash,
                    unspent_tx,
                    Params().GetConsensus(),
                    blockHash, true)) {

            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "Unable to find transaction with id: " + HexStr(c.out.hash));
        }

        // Generate a transaction and add it to the wallet so that CreateTransaction
        // can find and select it when getting and signing the transaction vin.
        CWalletTx unspent_wtx{&pwallet, unspent_tx};
        unspent_wtx.hashBlock = blockHash;
        unspent_wtx.nIndex = 0; //hack to get around not having CBlockIndex

        pwallet.AddToWallet(unspent_wtx);
        coin_control.Select({c.out.hash, static_cast<unsigned int>(c.out.n)});
        coin_control.fAllowWatchOnly = true;
    }
}

static UniValue EasyReceive(
        CWallet&  pwallet,
        const std::string& secret,
        const CPubKey& sender_pub,
        const std::string& optional_password,
        const int max_blocks,
        bool fSubtractFeeFromAmount,
        CWalletTx& wtx,
        CCoinControl& coin_control)
{
    if(max_blocks < 1 ) {
        throw JSONRPCError(
                RPC_PARSE_ERROR,
                "Error: maxblocks must be greater than 0");
    }

    if (pwallet.GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(
                RPC_CLIENT_P2P_DISABLED,
                "Error: Peer-to-peer functionality missing or disabled");
    }

    CKey escrow_key;

    //recreate the private/public key pair using secret and optional password.
    //We can then take the sender_pub and escrow_pub and generate a script that
    //matches the unspend script_id
    const auto mixedsecret = secret + optional_password;
    escrow_key.MakeNewKey(std::begin(mixedsecret), std::end(mixedsecret), COMPRESSED_KEY);
    auto escrow_pub = escrow_key.GetPubKey();

    auto easy_send_script = GetScriptForEasySend(max_blocks, sender_pub, escrow_pub);
    CScriptID script_id = easy_send_script;

    uint160 mixed_address;
    MixAddresses(script_id, sender_pub.GetID(), mixed_address);
    CScriptID easy_send_address{mixed_address};

    //Make sure to add keys and CScript before we create the transaction
    //because CreateTransaction assumes things are in your wallet.

    pwallet.AddReferralAddressPubKey(easy_send_address, sender_pub.GetID());
    pwallet.AddKeyPubKey(escrow_key, escrow_pub);
    pwallet.AddCScript(easy_send_script, easy_send_address);
    pwallet.SetAddressBook(easy_send_address, "", "easysend");

    EasySendCoins coins;
    CAmount unspent_amount = 0;

    FindEasySendCoins(easy_send_address, coins);
    SelectEasySendCoins(pwallet, coin_control, coins, unspent_amount);

    if(unspent_amount == 0) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "Coin has already been spent at address: " + EncodeDestination(easy_send_address));
    }

    // Reserve a key to accept the funds into.
    CReserveKey reserve_key(&pwallet);

    CPubKey receiver_pub;
    if (!reserve_key.GetReservedKey(receiver_pub)) {
        throw JSONRPCError(
                RPC_WALLET_ERROR,
                "Keypool ran out, please call keypoolrefill first");
    }

    CScript script_pub_key = GetScriptForDestination(receiver_pub.GetID());

    std::string error;
    std::vector<CRecipient> recipients = {
        {script_pub_key, unspent_amount, fSubtractFeeFromAmount}
    };

    int change_pos_ret = -1;
    CAmount fee_required = 0;


    if (!pwallet.CreateTransaction(
                recipients,
                wtx,
                reserve_key,
                fee_required,
                change_pos_ret,
                error,
                coin_control)) {

        throw JSONRPCError(RPC_WALLET_ERROR, error);
    }

    CValidationState state;
    if (!pwallet.CommitTransaction(wtx, reserve_key, g_connman.get(), state)) {
        error = strprintf(
                "Error: The transaction was rejected! Reason given: %s",
                state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, error);
    }

    //add script to wallet so we can redeem it later if needed.
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
    ret.push_back(Pair("amount", ValueFromAmount(unspent_amount)));

    return ret;
}

UniValue sendtoaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 8)
        throw std::runtime_error(
            "sendtoaddress \"address\" amount ( \"comment\" \"comment_to\" subtractfeefromamount replaceable conf_target \"estimate_mode\")\n"
            "\nSend an amount to a given address.\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"address\"            (string, required) The merit address to send to.\n"
            "2. \"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "3. \"comment\"            (string, optional) A comment used to store what the transaction is for. \n"
            "                             This is not part of the transaction, just kept in your wallet.\n"
            "4. \"comment_to\"         (string, optional) A comment to store the name of the person or organization \n"
            "                             to which you're sending the transaction. This is not part of the \n"
            "                             transaction, just kept in your wallet.\n"
            "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less merits than you enter in the amount field.\n"
            "6. replaceable            (boolean, optional) Allow this transaction to be replaced by a transaction with higher fees via BIP 125\n"
            "7. conf_target            (numeric, optional) Confirmation target (in blocks)\n"
            "8. \"estimate_mode\"      (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\"\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
            + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
            + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (!request.params[2].isNull() && !request.params[2].get_str().empty())
        wtx.mapValue["comment"] = request.params[2].get_str();
    if (!request.params[3].isNull() && !request.params[3].get_str().empty())
        wtx.mapValue["to"]      = request.params[3].get_str();

    bool fSubtractFeeFromAmount = false;
    if (!request.params[4].isNull()) {
        fSubtractFeeFromAmount = request.params[4].get_bool();
    }

    CCoinControl coin_control;
    if (!request.params[5].isNull()) {
        coin_control.signalRbf = request.params[5].get_bool();
    }

    if (!request.params[6].isNull()) {
        coin_control.m_confirm_target = ParseConfirmTarget(request.params[6]);
    }

    if (!request.params[7].isNull()) {
        if (!FeeModeFromString(request.params[7].get_str(), coin_control.m_fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
    }


    EnsureWalletIsUnlocked(pwallet);

    SendMoneyToDest(pwallet, dest, nAmount, fSubtractFeeFromAmount, wtx, coin_control);

    return wtx.GetHash().GetHex();
}

UniValue confirmaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "confirmaddress \"address\""
            "\nSend an amount to a given address.\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"address\"            (string, required) The merit address to send to.\n"
            "\nResult:\n"
            "\"txid\"                  (string) The invite transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Wallet comments
    CWalletTx wtx(true);

    CCoinControl coin_control;
    EnsureWalletIsUnlocked(pwallet);

    CScript scriptPubKey = GetScriptForDestination(dest);

    ConfirmAddress(pwallet, scriptPubKey, wtx, coin_control);

    return wtx.GetHash().GetHex();
}

UniValue easysend(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "easysend amount (\"password\", blocktimeout, subtractfeefromamount, \"estimate_mode\")\n"
            "\nSend an amount to a given channel.\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "2. \"password\"           (string) Optional password to further secure the transaction.\n"
            "3. blocktimeout           (numeric) The amount of blocks the transaction can be buried until the receiver cannot accept funds\n"
            "4. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less merits than you enter in the amount field.\n"
            "5. \"estimate_mode\"      (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\"\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id.\n"
            "\"pub\"                   (string) Escrow public key in hex.\n"
            "\nExamples:\n"
            + HelpExampleCli("easysend", "0.1")
            + HelpExampleCli("easysend", "0.1 abc124 100 true \"ECONOMICAL\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    // Amount
    CAmount amount = AmountFromValue(request.params[0]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    std::string optional_password = "";
    if(!request.params[1].isNull())
        optional_password = request.params[1].get_str();

    int max_blocks = 1008; //about a week.
    if(!request.params[2].isNull())
        max_blocks = request.params[2].get_int();

    // Wallet comments
    CWalletTx wtx;

    bool fSubtractFeeFromAmount = false;
    if (!request.params[3].isNull()) {
        fSubtractFeeFromAmount = request.params[3].get_bool();
    }

    CCoinControl coin_control;

    if (!request.params[4].isNull()) {
        if (!FeeModeFromString(
                    request.params[4].get_str(),
                    coin_control.m_fee_mode)) {

            throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    "Invalid estimate_mode parameter");
        }
    }

    EnsureWalletIsUnlocked(pwallet);

    return EasySend(
            *pwallet,
            amount,
            optional_password,
            max_blocks,
            fSubtractFeeFromAmount,
            wtx,
            coin_control);
}

UniValue easyreceive(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "easyreceive \"secret\" \"sender_pub_key\" (\"password\", blocktimeout) \n"
            "\nReceive an easy send transaction by providing secret and the sender public key.\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"secret\"            Secret used to access account in hex.\n"
            "2. \"sender_pub_key\"    Pubkey of sender.\n"
            "3. \"password\"          Optional password for transaction.\n"
            "4. \"blocktime\"         Optional amount of blocks the transaction can be buried under until cannot receive funds.\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id.\n"
            "\"amount\"                (string) Amount received.\n"
            "\nExamples:\n"
            + HelpExampleCli("easyreceive", "\"6acab82399\" \"024b4d5f9bba243314beb7739b964e16ef9a77d4b402d589976269569dd8718a09\"")
            + HelpExampleCli("easyreceive", "\"6acab82399\" \"024b4d5f9bba243314beb7739b964e16ef9a77d4b402d589976269569dd8718a09\" \"abc123\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    const auto secret_bytes = ParseHex(request.params[0].get_str());
    const std::string secret_str{std::begin(secret_bytes), std::end(secret_bytes)};

    CPubKey pub_key{ParseHex(request.params[1].get_str())};

    std::string optional_password = "";
    if(!request.params[2].isNull())
        optional_password = request.params[2].get_str();

    int max_blocks = 1008; //about a week.
    if(!request.params[3].isNull())
        max_blocks = request.params[3].get_int();

    // Wallet comments
    CWalletTx wtx;

    bool fSubtractFeeFromAmount = true;
    CCoinControl coin_control;

    EnsureWalletIsUnlocked(pwallet);

    return EasyReceive(
            *pwallet,
            secret_str,
            pub_key,
            optional_password,
            max_blocks,
            fSubtractFeeFromAmount,
            wtx,
            coin_control);
}

void ExtractWhitelist(const UniValue& options, vault::Whitelist& whitelist)
{
    const auto list = options["whitelist"].get_array();
    if(!list.isArray()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Whitelist must be a list");
    }

    for(size_t i = 0; i < list.size(); i++) {
        auto address_str = list[i].get_str();
        auto dest =  DecodeDestination(address_str);
        uint160 address;
        if(!GetUint160(dest, address)) {
            std::stringstream e;
            e << "The whitelist element \"" << address_str << "\" is not a valid address";
            throw JSONRPCError(RPC_TYPE_ERROR, e.str());
        }

        whitelist.push_back(ToByteVector(address));
    }
}

void ExtractPubKeys(const UniValue& list, vault::PubKeys& keys)
{
    if(!list.isArray()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "keys must be a list");
    }

    for(size_t i = 0; i < list.size(); i++) {
        auto key_str = list[i].get_str();
        CPubKey key{ParseHex(key_str)};

        if(!key.IsFullyValid()) {
            std::stringstream e;
            e << "The key element \"" << key_str << "\" is not a valid public key";
            throw JSONRPCError(RPC_TYPE_ERROR, e.str());
        }

        keys.push_back(key);
    }
}

std::vector<valtype> KeysToByteVectors(const vault::PubKeys& keys)
{
    std::vector<valtype> byte_vectors(keys.size());
    std::transform(keys.begin(), keys.end(), byte_vectors.begin(),
            [](const CPubKey& key) {
                return ToByteVector(key);
            });
    return byte_vectors;
}

UniValue createvault(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.empty()) {
        throw std::runtime_error(
            "createvault amount ({\"type\": \"...\", \"whitelist\": [...]})\n"
            "\nCreate a simple vault with a specific amount.\n"
            + HelpRequiringPassphrase(pwallet) +
            "\nArguments:\n"
            "1. \"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "2. \"options\"            (json) optional json object \n"
            "    {\n"
            "        \"type\": <\"simple\"| ...>, \n"
            "        \"spendlimit\": <amount merit> \n"
            "        \"whitelist\": [<address>,...], \n"
            "        \"spend_keys\": [<pubkey>,...], \n"
            "        \"master_keys\": [<pubkey>,...], \n"
            "    }\n"
            "\nResult:\n"
            "\"vault_address\"         (string) Address of the vault.\n"
            "\"txid\"                  (string) The transaction id creating the vault.\n"
            "\"amount\"                (number) Amount put in the vault.\n"
            "\"tag\"                   (string) Tag used to create the vault address.\n"
            "\"spend_pubkey_id\"       (string) Address of the key that can be used to spend from the vault.\n"
            "\"master_sk\"             (string) Master key used to update a vault. Save this.\n"
            "\"master_pk\"             (string) Master key public key. Save this.\n"
            "\nExamples:\n"
            + HelpExampleCli("createvault", "0.1")
            + HelpExampleCli("createvault", "0.1 {\"whitelist\": [\"key1\", \"key2\"]}")
        );
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount amount = AmountFromValue(request.params[0]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    std::string type = "simple";
    UniValue options;
    if(!request.params[1].isNull()) {
        RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});
        options = request.params[1].get_obj();
    }

    vault::Whitelist whitelist;

    CAmount spendlimit = 100000000_merit; //Default is max amount of merit in existence.
    if(options.isObject()) {
        if(options.exists("whitelist")) {
            ExtractWhitelist(options, whitelist);
        }

        if(options.exists("type")) {
            type = options["type"].get_str();
        }

        if(options.exists("spendlimit")) {
            spendlimit = AmountFromValue(options["spendlimit"]);
        }
    }

    UniValue ret(UniValue::VOBJ);
    UniValue whitelist_ret(UniValue::VARR);

    ret.push_back(Pair("type", type));

    if(type == "simple")
    {
        CReserveKey reserve_key(pwallet);

        CPubKey spend_pub_key;
        if(options.exists("spend_key")) {
            spend_pub_key = CPubKey{ParseHex(options["spend_key"].get_str())};
        } else {
            if (!reserve_key.GetReservedKey(spend_pub_key)) {
                throw JSONRPCError(
                        RPC_WALLET_ERROR,
                        "Keypool ran out, please call keypoolrefill first");
            }
        }

        auto spend_pub_key_id = spend_pub_key.GetID();


        CPubKey master_pub_key;
        CKey master_key;
        if(options.exists("master_key")) {
            master_pub_key = CPubKey{ParseHex(options["master_key"].get_str())};
        } else {
            master_key.MakeNewKey(true);
            master_pub_key = master_key.GetPubKey();
        }

        auto master_pub_key_id = master_pub_key.GetID();
        auto vault_tag = Hash160(master_pub_key_id.begin(), master_pub_key_id.end());
        auto vault_script = GetScriptForSimpleVault(vault_tag);

        //If the whitelist is not specified, just whitelist the spend key address.
        if(whitelist.empty()) {
            whitelist.push_back(ToByteVector(spend_pub_key_id));
            whitelist_ret.push_back(EncodeDestination(spend_pub_key_id));
        }

        CParamScriptID script_id = vault_script;

        referral::ReferralRef script_referral =
            pwallet->GenerateNewReferral(
                        script_id,
                        pwallet->ReferralAddress(),
                        pwallet->ReferralPubKey());

        if(!script_referral) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "Unable to generate referral for the vault script");
        }

        CParamScriptID vault_address{script_referral->GetAddress()};

        auto script_pub_key =
            GetParameterizedP2SH(
                    vault_address,
                    ToByteVector(spend_pub_key),
                    ToByteVector(master_pub_key),
                    spendlimit,
                    ExpandParam(whitelist),
                    whitelist.size(),
                    ToByteVector(vault_tag),
                    0 /* simple is type 0 */);


        CWalletTx wtx;
        CCoinControl no_coin_control; // This is a deprecated API
        SendMoney(pwallet, script_pub_key, amount, false, wtx, no_coin_control);

        const auto txid = wtx.GetHash().GetHex();

        pwallet->AddParamScript(vault_script, vault_address);

        ret.push_back(Pair("vault_address", EncodeDestination(vault_address)));
        ret.push_back(Pair("txid", txid));
        ret.push_back(Pair("amount", ValueFromAmount(amount)));
        ret.push_back(Pair("spendlimit", ValueFromAmount(spendlimit)));
        ret.push_back(Pair("script", ScriptToAsmStr(script_pub_key, true)));
        ret.push_back(Pair("vault_script", ScriptToAsmStr(vault_script, true)));
        ret.push_back(Pair("tag", vault_tag.GetHex()));
        ret.push_back(Pair("spend_pubkey_id", EncodeDestination(spend_pub_key_id)));
        if(master_key.IsValid()) {
            ret.push_back(Pair("master_sk", CMeritSecret(master_key).ToString()));
        }
        ret.push_back(Pair("master_pk", HexStr(master_pub_key.begin(), master_pub_key.end())));

    } else if (type == "multisig") {

        vault::PubKeys spend_keys;
        if(!options.exists("spend_keys")) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a spender public key list");
        }

        ExtractPubKeys(options["spend_keys"].get_array(), spend_keys);
        if(spend_keys.empty()) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a non empty spend_keys list");

        }

        vault::PubKeys master_keys;
        if(!options.exists("master_keys")) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a master public key list");
        }

        ExtractPubKeys(options["master_keys"].get_array(), master_keys);
        if(master_keys.empty()) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a non empty master_keys list");

        }

        const auto& tag_seed = master_keys[0];
        auto vault_tag = Hash160(tag_seed.begin(), tag_seed.end());
        auto vault_script = GetScriptForMultisigVault(vault_tag);

        //If the whitelist is not specified, just whitelist the spend keys.
        if(whitelist.empty()) {
            std::transform(spend_keys.begin(), spend_keys.end(),
                    std::back_inserter(whitelist),
                    [&whitelist_ret](const CPubKey& key) {
                        auto key_id = key.GetID();
                        whitelist_ret.push_back(EncodeDestination(key_id));
                        return ToByteVector(key_id);
                    });
        }

        CParamScriptID script_id = vault_script;

        referral::ReferralRef script_referral =
            pwallet->GenerateNewReferral(
                        script_id,
                        pwallet->ReferralAddress(),
                        pwallet->ReferralPubKey());

        if(!script_referral) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "Unable to generate referral for the vault script");
        }

        CParamScriptID vault_address{script_referral->GetAddress()};

        auto spend_key_vectors = KeysToByteVectors(spend_keys);
        auto master_key_vectors = KeysToByteVectors(master_keys);

        auto script_pub_key =
            GetParameterizedP2SH(
                    vault_address,
                    ExpandParam(spend_key_vectors),
                    spend_key_vectors.size(),
                    ExpandParam(master_key_vectors),
                    master_key_vectors.size(),
                    spendlimit,
                    ExpandParam(whitelist),
                    whitelist.size(),
                    ToByteVector(vault_tag),
                    1);

        CWalletTx wtx;
        CCoinControl no_coin_control; // This is a deprecated API
        SendMoney(pwallet, script_pub_key, amount, false, wtx, no_coin_control);

        const auto txid = wtx.GetHash().GetHex();

        pwallet->AddParamScript(vault_script, vault_address);

        ret.push_back(Pair("vault_address", EncodeDestination(vault_address)));
        ret.push_back(Pair("txid", txid));
        ret.push_back(Pair("amount", ValueFromAmount(amount)));
        ret.push_back(Pair("spendlimit", ValueFromAmount(spendlimit)));
        ret.push_back(Pair("script", ScriptToAsmStr(script_pub_key, true)));
        ret.push_back(Pair("vault_script", ScriptToAsmStr(vault_script, true)));
        ret.push_back(Pair("tag", vault_tag.GetHex()));
        ret.push_back(Pair("spend_keys", options["spend_keys"].get_array()));
        ret.push_back(Pair("master_keys", options["spend_keys"].get_array()));

    } else {
        throw JSONRPCError(RPC_TYPE_ERROR, "The type \"" + type + "\" is not valid");
    }

    if(options.exists("whitelist")) {
        whitelist_ret = options["whitelist"].get_array();
    }

    ret.push_back(Pair("whitelist", whitelist_ret));

    return ret;
}

UniValue renewvault(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2) {
        throw std::runtime_error(
                "renewvault vault_address (options)\n"
                "\nCreate a simple vault with a specific amount.\n"
                + HelpRequiringPassphrase(pwallet) +
                "\nArguments:\n"
                "1. \"vault_address\"      (string) Address of the vault.\n"
                "2. \"options\"            (json object) Options about which parts of the vault to change.\n"
                "       {\n"
                "           \"whitelist\": [\"addr1\", ...],\n"
                "           \"master_sk\": \"master secret key in wif\",\n"
                "           \"new_master_sk\": \"master secret key in hex\",\n"
                "           \"new_master_pk\": \"master public key in hex\",\n"
                "           \"new_spend_pk\": \"master public key in hex\"\n"
                "       }\n"
                "\nResult:\n"
                "\"txid\"                  (string) The transaction id.\n"
                "\"amount\"          (string) Address of the vault.\n"
                "\nExamples:\n"
                + HelpExampleCli("renewvault", "2NFg1HWEUKd7ipSjnmMVUySXgQ18MeUChyz <master secrety key>")
                + HelpExampleCli("renewvault", "2NFg1HWEUKd7ipSjnmMVUySXgQ18MeUChyz <master secrety key> '{\"whitelist\":[\"mjFifGXWS9JptwS2D2UjQAjG4G6jQqwXc9\"]}'")
                );
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string address = request.params[0].get_str();

    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    auto script_id = boost::get<CParamScriptID>(&dest);
    if(!script_id) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Parameterized Script Address Required");
    }

    UniValue options;
    if(!request.params[1].isNull()) {
        RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ});
        options = request.params[1].get_obj();
    }

    auto unspent_coins = vault::FindUnspentVaultCoins(*script_id);

    if(unspent_coins.empty()) {
        throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                "Cannot find the vault by the address specified");
    }

    const auto vaults = vault::ParseVaultCoins(unspent_coins);
    assert(!vaults.empty());

    const auto total_amount =
        std::accumulate(vaults.begin(), vaults.end(), CAmount{0},
           [](const CAmount t, const vault::Vault& v) {
                return t + v.coin.out.nValue;
           });

    CCoinControl coin_control;
    for(const auto& vault : vaults) {
        coin_control.Select(vault.out_point);
    }

    coin_control.fAllowWatchOnly = true;

    const auto& vault = vaults[0];

    //Make sure to add keys and CScript before we create the transaction
    //because CreateTransaction assumes things are in your wallet.
    pwallet->AddParamScript(vault.script, *script_id);

    bool subtract_fee_from_amount = true;

    vault::Whitelist whitelist = vault.whitelist;
    if(options.exists("whitelist")) {
        whitelist.clear();
        ExtractWhitelist(options, whitelist);
    }

    if(whitelist.empty()) {
        std::stringstream e;
        throw JSONRPCError(
                RPC_INVALID_PARAMS,
                "New whitelist must have at least one address");
    }

    auto spendlimit = vault.spendlimit;
    if(options.exists("spendlimit")) {
        spendlimit = AmountFromValue(options["spendlimit"]);
    }

    UniValue ret(UniValue::VOBJ);

    if(vault.type == 0) {

        CKey master_key;
        if(options.exists("orig_master_sk")) {
            CMeritSecret master_secret;
            master_secret.SetString(options["orig_master_sk"].get_str());
            master_key = master_secret.GetKey();
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Must provide orig_master_sk");
        }

        auto spend_pub_key = vault.spend_pub_key;
        if(options.exists("spend_pk")) {
            auto spend_pk_bytes = ParseHex(options["spend_pk"].get_str());
            spend_pub_key.Set(spend_pk_bytes.begin(), spend_pk_bytes.end());
        }

        auto master_pub_key = vault.master_pub_key;
        if(options.exists("master_pk") && options.exists("new_master_sk")) {
            CMeritSecret master_secret;
            master_secret.SetString(options["new_master_sk"].get_str());

            auto new_master_key = master_secret.GetKey();
            const CPrivKey master_sk = new_master_key.GetPrivKey();

            auto master_pk_bytes = ParseHex(options["master_pk"].get_str());
            master_pub_key.Set(master_pk_bytes.begin(), master_pk_bytes.end());

            CKey check_master_key;
            if(!check_master_key.Load(master_sk, master_pub_key, false)) {
                throw JSONRPCError(
                        RPC_INVALID_PARAMS,
                        "The new master private key provided isn't a valid"
                        " private key given the public key provided.");
            }
        }

        if(whitelist.empty()) {
            std::stringstream e;
            throw JSONRPCError(
                    RPC_INVALID_PARAMS,
                    "New whitelist must have at least one address");
        }

        const auto script_pub_key =
            GetParameterizedP2SH(
                    *script_id,
                    ToByteVector(spend_pub_key),
                    ToByteVector(master_pub_key),
                    spendlimit,
                    ExpandParam(whitelist),
                    whitelist.size(),
                    ToByteVector(vault.tag),
                    0 /* simple is type 0 */);

        //TODO: create script with different spend key
        //TODO: validate all unspent coins have the same vault param.
        std::vector<CRecipient> recipients = {
            {script_pub_key, total_amount, subtract_fee_from_amount}
        };

        CWalletTx wtx;
        CReserveKey reserve_key(pwallet);
        CAmount fee_required = 0;
        int change_pos_ret = -1;
        std::string error;
        const bool SIGN = true;

        if (!pwallet->CreateTransaction(
                    recipients,
                    wtx,
                    reserve_key,
                    fee_required,
                    change_pos_ret,
                    error,
                    coin_control,
                    !SIGN)) {

            throw JSONRPCError(RPC_WALLET_ERROR, error);
        }

        assert(wtx.tx);

        CMutableTransaction mtx{*wtx.tx};

        assert(mtx.vin.size() == vaults.size());

        const auto& referral_pub_key_id = pwallet->ReferralPubKey().GetID();

        for(size_t i = 0; i <  mtx.vin.size(); i++) {
            auto& in = mtx.vin[i];
            const auto& vault = vaults[i];

            uint256 hash = SignatureHash(
                    vault.script,
                    *wtx.tx,
                    i,
                    SIGHASH_ALL,
                    vault.coin.out.nValue,
                    SIGVERSION_BASE);

            //produce canonical DER signature
            valtype sig;
            if(!master_key.Sign(hash, sig))
                return false;
            sig.push_back(SIGHASH_ALL);


            const int RENEW_MODE = 1;
            in.scriptSig
                << sig
                << RENEW_MODE
                << valtype{referral_pub_key_id.begin(), referral_pub_key_id.end()}
            << valtype{vault.script.begin(), vault.script.end()};
        }

        wtx.SetTx(std::make_shared<CTransaction>(mtx));

        CValidationState state;
        if (!pwallet->CommitTransaction(wtx, reserve_key, g_connman.get(), state)) {
            error = strprintf(
                    "Error: The transaction was rejected! Reason given: %s",
                    state.GetRejectReason());
            throw JSONRPCError(RPC_WALLET_ERROR, error);
        }

        //add script to wallet so we can redeem it later if needed.
        ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
        ret.push_back(Pair("amount", ValueFromAmount(total_amount)));
    } else if (vault.type == 1) {

        vault::PubKeys spend_keys = vault.spend_keys;
        if(!options.exists("spend_keys")) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a spender public key list");
        }

        ExtractPubKeys(options["spend_keys"].get_array(), spend_keys);
        if(spend_keys.empty()) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a non empty spend_keys list");

        }

        vault::PubKeys master_keys = vault.master_keys;
        if(!options.exists("master_keys")) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a master public key list");
        }

        ExtractPubKeys(options["master_keys"].get_array(), master_keys);
        if(master_keys.empty()) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "must specify a non empty master_keys list");

        }

        auto spend_key_vectors = KeysToByteVectors(spend_keys);
        auto master_key_vectors = KeysToByteVectors(master_keys);

        auto script_pub_key =
            GetParameterizedP2SH(
                    *script_id,
                    ExpandParam(spend_key_vectors),
                    spend_key_vectors.size(),
                    ExpandParam(master_key_vectors),
                    master_key_vectors.size(),
                    spendlimit,
                    ExpandParam(whitelist),
                    whitelist.size(),
                    ToByteVector(vault.tag),
                    1);

        //TODO: create script with different spend key
        //TODO: validate all unspent coins have the same vault param.
        std::vector<CRecipient> recipients = {
            {script_pub_key, total_amount, subtract_fee_from_amount}
        };

        CWalletTx wtx;
        CReserveKey reserve_key(pwallet);
        CAmount fee_required = 0;
        int change_pos_ret = -1;
        std::string error;
        const bool SIGN = true;

        if (!pwallet->CreateTransaction(
                    recipients,
                    wtx,
                    reserve_key,
                    fee_required,
                    change_pos_ret,
                    error,
                    coin_control,
                    !SIGN)) {

            throw JSONRPCError(RPC_WALLET_ERROR, error);
        }

        assert(wtx.tx);

        CMutableTransaction mtx{*wtx.tx};

        assert(mtx.vin.size() == vaults.size());

        const auto& referral_pub_key_id = pwallet->ReferralPubKey().GetID();

        for(size_t i = 0; i <  mtx.vin.size(); i++) {
            auto& in = mtx.vin[i];
            const auto& vault = vaults[i];

            //We don't put the sig here because we will use "signrawtransaction"
            const int RENEW_MODE = 1;
            in.scriptSig
                << RENEW_MODE
                << valtype{referral_pub_key_id.begin(), referral_pub_key_id.end()}
            << valtype{vault.script.begin(), vault.script.end()};
        }

        wtx.SetTx(std::make_shared<CTransaction>(mtx));

        //add script to wallet so we can redeem it later if needed.
        ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
        ret.push_back(Pair("amount", ValueFromAmount(total_amount)));
        ret.push_back(Pair("rawtx", EncodeHexTx(*wtx.tx)));

    } else {
        std::stringstream e;
        e << "Unknown vault type " << vault.type;
        throw JSONRPCError(RPC_WALLET_ERROR, e.str());
    }

    return ret;
}

UniValue spendvault(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 3) {
        throw std::runtime_error(
                "spendvault vault_address amount destination_address\n"
                "\nSpends the amount specified to the destination address.\n"
                + HelpRequiringPassphrase(pwallet) +
                "\nArguments:\n"
                "1. \"vault_address\"       (string) Address of the vault.\n"
                "2. \"amount\"              (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
                "3. \"destination_address\" (string) Destination of funds.\n"
                "4. \"signing key\"         (string) Optional Hex string of the spending key.\n"
                "5. \"send\"                (bool) Optional send or just print out tx. default is true.\n"
                "\nResult:\n"
                "\"txid\"                   (string) The transaction id.\n"
                "\"amount\"                 (number) amount sent.\n"
                "\nExamples:\n"
                + HelpExampleCli("spendvault", "2NFg1HWEUKd7ipSjnmMVUySXgQ18MeUChyz 5 1NAg1HWEUKd7ipSjnmMVUySXgQ18Mezfjyz")
                );
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string vault_address = request.params[0].get_str();

    CAmount amount = AmountFromValue(request.params[1]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    std::string dest_address = request.params[2].get_str();

    std::string spend_key_wif;
    if(!request.params[3].isNull()) {
        spend_key_wif = request.params[3].get_str();
    }

    bool send = request.params[4].isNull() ?  true : request.params[4].get_bool();

    CTxDestination vault_dest = DecodeDestination(vault_address);
    if (boost::get<CParamScriptID>(&vault_dest) == nullptr) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid vault address");
    }

    CTxDestination dest = DecodeDestination(dest_address);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid destination address");
    }

    auto script_id = boost::get<CParamScriptID>(&vault_dest);
    if(!script_id) {
        throw JSONRPCError(RPC_TYPE_ERROR, "The vault address must be a parameterized script address");
    }

    auto unspent_coins = vault::FindUnspentVaultCoins(*script_id);

    if(unspent_coins.empty()) {
        throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                "Cannot find the vault by the address specified");
    }

    UniValue ret(UniValue::VOBJ);

    const auto vaults = vault::ParseVaultCoins(unspent_coins);
    assert(!vaults.empty());

    const auto& vault = vaults[0];

    const auto total_amount =
        std::accumulate(vaults.begin(), vaults.end(), CAmount{0},
           [](const CAmount t, const vault::Vault& v) {
                return t + v.coin.out.nValue;
           });

    if(amount > total_amount) {
        std::stringstream e;
        e << "Insufficient funds, can only spend "
          << ValueFromAmount(total_amount).get_real()
          << " merit";
        throw JSONRPCError(RPC_TYPE_ERROR, e.str());
    }

    if(amount > vaults[0].spendlimit) {
        std::stringstream e;
        e << "Amount is over the spend limit of "
          << ValueFromAmount(vaults[0].spendlimit).get_real()
          << " merit";
        throw JSONRPCError(RPC_TYPE_ERROR, e.str());
    }

    //Select enough coins to satisfy the amount we want to send.
    //At this point we should have enough coins.
    CCoinControl coin_control;
    CAmount selected_amount = 0;
    for(const auto& v : vaults) {
        if(selected_amount >= amount) break;
        coin_control.Select(v.out_point);
        selected_amount += v.coin.out.nValue;
    }

    assert(selected_amount >= amount);
    auto change = selected_amount - amount;

    coin_control.fAllowWatchOnly = true;

    //Make sure to add keys and CScript before we create the transaction
    //because CreateTransaction assumes things are in your wallet.
    pwallet->AddParamScript(vaults[0].script, *script_id);

    //The two recipients are the spend key and the vault.
    //If there is change the change will go into the same vault.
    //The order of the recipients is important because the vault script requires
    //the first is the spend key and the second is the vault where changes goes into.
    bool subtract_fee_from_amount = true;

    auto scriptPubKey = GetScriptForDestination(dest);

    std::vector<CRecipient> recipients = {
        {scriptPubKey, amount, subtract_fee_from_amount},
    };

    //TODO: Currently vault scipt requires that there is change. The script
    //will need to be updated to have a new mode to drain vault of all funds.
    if(change > 0) {
        recipients.push_back({vaults[0].coin.out.scriptPubKey, change, false});
    }

    CWalletTx wtx;
    CReserveKey reserve_key(pwallet);
    CAmount fee_required = 0;
    int change_pos_ret = -1;
    std::string error;
    const bool SIGN = true;

    if (!pwallet->CreateTransaction(
                recipients,
                wtx,
                reserve_key,
                fee_required,
                change_pos_ret,
                error,
                coin_control,
                !SIGN)) {

        throw JSONRPCError(RPC_WALLET_ERROR, error);
    }

    assert(wtx.tx);

    CMutableTransaction mtx{*wtx.tx};

    const auto spend_address = vaults[0].spend_pub_key.GetID();

    CKey spend_key;
    if(spend_key_wif.empty()) {
        if (!pwallet->GetKey(spend_address, spend_key)) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR, "Unable to find the spendkey in the keystore");
        }
    } else  {
        CMeritSecret spend_secret;
        spend_secret.SetString(spend_key_wif);
        spend_key = spend_secret.GetKey();
    }

    const auto& referral_pub_key_id = pwallet->ReferralPubKey().GetID();

    for(size_t i = 0; i <  mtx.vin.size(); i++) {
        auto& in = mtx.vin[i];
        const auto& vault = vaults[i];
        const int SPEND_MODE = 0;

        if(vault.type == 0) {

            //TODO: Sign transaction and insert params
            uint256 hash = SignatureHash(
                    vault.script,
                    *wtx.tx,
                    i,
                    SIGHASH_ALL,
                    vault.coin.out.nValue,
                    SIGVERSION_BASE);

            //produce canonical DER signature
            valtype sig;
            if(!spend_key.Sign(hash, sig))
                return false;
            sig.push_back(SIGHASH_ALL);

            in.scriptSig
                << sig
                << SPEND_MODE
                << valtype{referral_pub_key_id.begin(), referral_pub_key_id.end()}
                << valtype{vault.script.begin(), vault.script.end()};

        } else if (vault.type == 1) {
            const int SPEND_MODE = 0;
            in.scriptSig
                << SPEND_MODE
                << valtype{referral_pub_key_id.begin(), referral_pub_key_id.end()}
                << valtype{vault.script.begin(), vault.script.end()};
        }
    }

    wtx.SetTx(std::make_shared<CTransaction>(mtx));

    if(vault.type == 1) {
        send = false;
    }

    if(send) {
        CValidationState state;
        if (!pwallet->CommitTransaction(wtx, reserve_key, g_connman.get(), state)) {
            error = strprintf(
                    "Error: The transaction was rejected! Reason given: %s",
                    state.GetRejectReason());
            throw JSONRPCError(RPC_WALLET_ERROR, error);
        }
    }

    ret.push_back(Pair("txid", wtx.GetHash().GetHex()));
    ret.push_back(Pair("amount", ValueFromAmount(amount)));

    if(!send) {
        ret.push_back(Pair("rawtx", EncodeHexTx(*wtx.tx)));
    }


    return ret;
}

UniValue getvaultinfo(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
                "getvaultinfo vault_address\n"
                "\nGet vault info.\n"
                + HelpRequiringPassphrase(pwallet) +
                "\nArguments:\n"
                "1. \"vault_address\"      (string) Address of the vault.\n"
                "\nResult:\n"
                "\"address\"               (string) The transaction id.\n"
                "\"type\"                  (string) Address of the vault.\n"
                "\nExamples:\n"
                + HelpExampleCli("getvaultinfo", "2NFg1HWEUKd7ipSjnmMVUySXgQ18MeUChyz")
                );
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string address = request.params[0].get_str();

    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    auto script_id = boost::get<CParamScriptID>(&dest);
    if(!script_id) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Parameterized Script Address Required");
    }

    auto unspent_coins = vault::FindUnspentVaultCoins(*script_id);

    if(unspent_coins.empty()) {
        throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                "Cannot find the vault by the address specified");
    }

    const auto vaults = vault::ParseVaultCoins(unspent_coins);
    assert(!vaults.empty());

    UniValue ret(UniValue::VOBJ);
    UniValue coins(UniValue::VARR);
    UniValue whitelist(UniValue::VARR);

    CAmount total_amount = 0;
    bool consistent = true;

    const auto& ref = vaults[0];

    for(const auto& v : vaults) {
        size_t confirmations = std::max(0, chainActive.Height() - v.coin.nHeight);

        UniValue c(UniValue::VOBJ);
        c.push_back(Pair("txid", v.txid.GetHex()));
        c.push_back(Pair("index", static_cast<int>(v.out_point.n)));
        c.push_back(Pair("amount", ValueFromAmount(v.coin.out.nValue)));
        c.push_back(Pair("confirmations", static_cast<int>(confirmations)));

        coins.push_back(c);

        if(!v.SameKind(ref)) {
            c.push_back(Pair("consistent", false));
            consistent = false;
        }

        total_amount += v.coin.out.nValue;
    }

    //add script to wallet so we can redeem it later if needed.
    ret.push_back(Pair("type", ref.type));
    ret.push_back(Pair("address", address));
    ret.push_back(Pair("amount", ValueFromAmount(total_amount)));
    ret.push_back(Pair("spendlimit", ValueFromAmount(ref.spendlimit)));
    ret.push_back(Pair("coins", coins));
    ret.push_back(Pair("consistent", consistent));
    ret.push_back(Pair("spend_pub_key", HexStr(ref.spend_pub_key)));
    ret.push_back(Pair("master_pub_key", HexStr(ref.master_pub_key)));

    return ret;
}

UniValue listaddressgroupings(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "listaddressgroupings\n"
            "\nLists groups of addresses which have had their common ownership\n"
            "made public by common use as inputs or as the resulting change\n"
            "in past transactions\n"
            "\nResult:\n"
            "[\n"
            "  [\n"
            "    [\n"
            "      \"address\",            (string) The merit address\n"
            "      amount,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
            "      \"account\"             (string, optional) DEPRECATED. The account\n"
            "    ]\n"
            "    ,...\n"
            "  ]\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listaddressgroupings", "")
            + HelpExampleRpc("listaddressgroupings", "")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = pwallet->GetAddressBalances();
    for (const std::set<CTxDestination>& grouping : pwallet->GetAddressGroupings()) {
        UniValue jsonGrouping(UniValue::VARR);
        for (const CTxDestination& address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(EncodeDestination(address));
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                if (pwallet->mapAddressBook.find(address) != pwallet->mapAddressBook.end()) {
                    addressInfo.push_back(pwallet->mapAddressBook.find(address)->second.name);
                }
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

UniValue signmessage(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "signmessage \"address\" \"message\"\n"
            "\nSign a message with the private key of an address"
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The merit address to use for the private key.\n"
            "2. \"message\"         (string, required) The message to create a signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\", \"my message\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    CKey key;
    if (!pwallet->GetKey(*keyID, key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue getreceivedbyaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getreceivedbyaddress \"address\" ( minconf )\n"
            "\nReturns the total amount received by the given address in transactions with at least minconf confirmations.\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The merit address for transactions.\n"
            "2. minconf             (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount   (numeric) The total amount in " + CURRENCY_UNIT + " received at this address.\n"
            "\nExamples:\n"
            "\nThe amount from transactions with at least 1 confirmation\n"
            + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\"") +
            "\nThe amount including unconfirmed transactions, zero confirmations\n"
            + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" 0") +
            "\nThe amount with at least 6 confirmations\n"
            + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\", 6")
       );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    // Merit address
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Merit address");
    }
    CScript scriptPubKey = GetScriptForDestination(dest);
    if (!IsMine(*pwallet, scriptPubKey)) {
        return ValueFromAmount(0);
    }

    // Minimum confirmations
    int nMinDepth = 1;
    if (!request.params[1].isNull())
        nMinDepth = request.params[1].get_int();

    // Tally
    CAmount nAmount = 0;
    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        if (wtx.IsCoinBase() || !CheckFinalTx(*wtx.tx))
            continue;

        for (const CTxOut& txout : wtx.tx->vout)
            if (txout.scriptPubKey == scriptPubKey)
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


UniValue getreceivedbyaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getreceivedbyaccount \"account\" ( minconf )\n"
            "\nDEPRECATED. Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.\n"
            "\nArguments:\n"
            "1. \"account\"      (string, required) The selected account, may be the default account using \"\".\n"
            "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
            "\nExamples:\n"
            "\nAmount received by the default account with at least 1 confirmation\n"
            + HelpExampleCli("getreceivedbyaccount", "\"\"") +
            "\nAmount received at the tabby account including unconfirmed amounts with zero confirmations\n"
            + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 0") +
            "\nThe amount with at least 6 confirmations\n"
            + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getreceivedbyaccount", "\"tabby\", 6")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    // Minimum confirmations
    int nMinDepth = 1;
    if (!request.params[1].isNull())
        nMinDepth = request.params[1].get_int();

    // Get the set of pub keys assigned to account
    std::string strAccount = AccountFromValue(request.params[0]);
    std::set<CTxDestination> setAddress = pwallet->GetAccountAddresses(strAccount);

    // Tally
    CAmount nAmount = 0;
    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        if (wtx.IsCoinBase() || !CheckFinalTx(*wtx.tx))
            continue;

        for (const CTxOut& txout : wtx.tx->vout)
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*pwallet, address) && setAddress.count(address)) {
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
            }
        }
    }

    return ValueFromAmount(nAmount);
}


UniValue getbalance(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "getbalance ( \"account\" minconf include_watchonly )\n"
            "\nIf account is not specified, returns the server's total available balance.\n"
            "If account is specified (DEPRECATED), returns the balance in the account.\n"
            "Note that the account \"\" is not the same as leaving the parameter out.\n"
            "The server total may be different to the balance in the default \"\" account.\n"
            "\nArguments:\n"
            "1. \"account\"         (string, optional) DEPRECATED. The account string may be given as a\n"
            "                     specific account name to find the balance associated with wallet keys in\n"
            "                     a named account, or as the empty string (\"\") to find the balance\n"
            "                     associated with wallet keys not in any named account, or as \"*\" to find\n"
            "                     the balance associated with all wallet keys regardless of account.\n"
            "                     When this option is specified, it calculates the balance in a different\n"
            "                     way than when it is not specified, and which can count spends twice when\n"
            "                     there are conflicting pending transactions (such as those created by\n"
            "                     the bumpfee command), temporarily resulting in low or even negative\n"
            "                     balances. In general, account balance calculation is not considered\n"
            "                     reliable and has resulted in confusing outcomes, so it is recommended to\n"
            "                     avoid passing this argument.\n"
            "2. minconf           (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "3. include_watchonly (bool, optional, default=false) Also include balance in watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
            "\nExamples:\n"
            "\nThe total amount in the wallet with 1 or more confirmations\n"
            + HelpExampleCli("getbalance", "") +
            "\nThe total amount in the wallet at least 6 blocks confirmed\n"
            + HelpExampleCli("getbalance", "\"*\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getbalance", "\"*\", 6")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    const UniValue& account_value = request.params[0];
    const UniValue& minconf = request.params[1];
    const UniValue& include_watchonly = request.params[2];

    if (account_value.isNull()) {
        if (!minconf.isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                "getbalance minconf option is only currently supported if an account is specified");
        }
        if (!include_watchonly.isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                "getbalance include_watchonly option is only currently supported if an account is specified");
        }
        return ValueFromAmount(pwallet->GetBalance());
    }

    const std::string& account_param = account_value.get_str();
    const std::string* account = account_param != "*" ? &account_param : nullptr;

    int nMinDepth = 1;
    if (!minconf.isNull())
        nMinDepth = minconf.get_int();
    isminefilter filter = ISMINE_SPENDABLE;
    if(!include_watchonly.isNull())
        if(include_watchonly.get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    return ValueFromAmount(pwallet->GetLegacyBalance(filter, nMinDepth, account));
}

UniValue getunconfirmedbalance(const JSONRPCRequest &request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
                "getunconfirmedbalance\n"
                "Returns the server's total unconfirmed balance\n");

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    return ValueFromAmount(pwallet->GetUnconfirmedBalance());
}


UniValue movecmd(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 5)
        throw std::runtime_error(
            "move \"fromaccount\" \"toaccount\" amount ( minconf \"comment\" )\n"
            "\nDEPRECATED. Move a specified amount from one account in your wallet to another.\n"
            "\nArguments:\n"
            "1. \"fromaccount\"   (string, required) The name of the account to move funds from. May be the default account using \"\".\n"
            "2. \"toaccount\"     (string, required) The name of the account to move funds to. May be the default account using \"\".\n"
            "3. amount            (numeric) Quantity of " + CURRENCY_UNIT + " to move between accounts.\n"
            "4. (dummy)           (numeric, optional) Ignored. Remains for backward compatibility.\n"
            "5. \"comment\"       (string, optional) An optional comment, stored in the wallet only.\n"
            "\nResult:\n"
            "true|false           (boolean) true if successful.\n"
            "\nExamples:\n"
            "\nMove 0.01 " + CURRENCY_UNIT + " from the default account to the account named tabby\n"
            + HelpExampleCli("move", "\"\" \"tabby\" 0.01") +
            "\nMove 0.01 " + CURRENCY_UNIT + " timotei to akiko with a comment and funds have 6 confirmations\n"
            + HelpExampleCli("move", "\"timotei\" \"akiko\" 0.01 6 \"happy birthday!\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("move", "\"timotei\", \"akiko\", 0.01, 6, \"happy birthday!\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strFrom = AccountFromValue(request.params[0]);
    std::string strTo = AccountFromValue(request.params[1]);
    CAmount nAmount = AmountFromValue(request.params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    if (!request.params[3].isNull())
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)request.params[3].get_int();
    std::string strComment;
    if (!request.params[4].isNull())
        strComment = request.params[4].get_str();

    if (!pwallet->AccountMove(strFrom, strTo, nAmount, strComment)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");
    }

    return true;
}


UniValue sendfrom(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 3 || request.params.size() > 6)
        throw std::runtime_error(
            "sendfrom \"fromaccount\" \"toaddress\" amount ( minconf \"comment\" \"comment_to\" )\n"
            "\nDEPRECATED (use sendtoaddress). Sent an amount from an account to a merit address."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"fromaccount\"       (string, required) The name of the account to send funds from. May be the default account using \"\".\n"
            "                       Specifying an account does not influence coin selection, but it does associate the newly created\n"
            "                       transaction with the account, so the account's balance computation and transaction history can reflect\n"
            "                       the spend.\n"
            "2. \"toaddress\"         (string, required) The merit address to send funds to.\n"
            "3. amount                (numeric or string, required) The amount in " + CURRENCY_UNIT + " (transaction fee is added on top).\n"
            "4. minconf               (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
            "5. \"comment\"           (string, optional) A comment used to store what the transaction is for. \n"
            "                                     This is not part of the transaction, just kept in your wallet.\n"
            "6. \"comment_to\"        (string, optional) An optional comment to store the name of the person or organization \n"
            "                                     to which you're sending the transaction. This is not part of the transaction, \n"
            "                                     it is just kept in your wallet.\n"
            "\nResult:\n"
            "\"txid\"                 (string) The transaction id.\n"
            "\nExamples:\n"
            "\nSend 0.01 " + CURRENCY_UNIT + " from the default account to the address, must have at least 1 confirmation\n"
            + HelpExampleCli("sendfrom", "\"\" \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01") +
            "\nSend 0.01 from the tabby account to the given address, funds must have at least 6 confirmations\n"
            + HelpExampleCli("sendfrom", "\"tabby\" \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01 6 \"donation\" \"seans outpost\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendfrom", "\"tabby\", \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.01, 6, \"donation\", \"seans outpost\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAccount = AccountFromValue(request.params[0]);
    CTxDestination dest = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Merit address");
    }
    CAmount nAmount = AmountFromValue(request.params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    int nMinDepth = 1;
    if (!request.params[3].isNull())
        nMinDepth = request.params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (!request.params[4].isNull() && !request.params[4].get_str().empty())
        wtx.mapValue["comment"] = request.params[4].get_str();
    if (!request.params[5].isNull() && !request.params[5].get_str().empty())
        wtx.mapValue["to"]      = request.params[5].get_str();

    EnsureWalletIsUnlocked(pwallet);

    // Check funds
    CAmount nBalance = pwallet->GetLegacyBalance(ISMINE_SPENDABLE, nMinDepth, &strAccount);
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    CCoinControl no_coin_control; // This is a deprecated API
    SendMoneyToDest(pwallet, dest, nAmount, false, wtx, no_coin_control);

    return wtx.GetHash().GetHex();
}


UniValue sendmany(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 8)
        throw std::runtime_error(
            "sendmany \"fromaccount\" {\"address\":amount,...} ( minconf \"comment\" [\"address\",...] replaceable conf_target \"estimate_mode\")\n"
            "\nSend multiple times. Amounts are double-precision floating point numbers."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments:\n"
            "1. \"fromaccount\"         (string, required) DEPRECATED. The account to send the funds from. Should be \"\" for the default account\n"
            "2. \"amounts\"             (string, required) A json object with addresses and amounts\n"
            "    {\n"
            "      \"address\":amount   (numeric or string) The merit address is the key, the numeric amount (can be string) in " + CURRENCY_UNIT + " is the value\n"
            "      ,...\n"
            "    }\n"
            "3. minconf                 (numeric, optional, default=1) Only use the balance confirmed at least this many times.\n"
            "4. \"comment\"             (string, optional) A comment\n"
            "5. subtractfeefrom         (array, optional) A json array with addresses.\n"
            "                           The fee will be equally deducted from the amount of each selected address.\n"
            "                           Those recipients will receive less merits than you enter in their corresponding amount field.\n"
            "                           If no addresses are specified here, the sender pays the fee.\n"
            "    [\n"
            "      \"address\"          (string) Subtract fee from this address\n"
            "      ,...\n"
            "    ]\n"
            "6. replaceable            (boolean, optional) Allow this transaction to be replaced by a transaction with higher fees via BIP 125\n"
            "7. conf_target            (numeric, optional) Confirmation target (in blocks)\n"
            "8. \"estimate_mode\"      (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\"\n"
             "\nResult:\n"
            "\"txid\"                   (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\"") +
            "\nSend two amounts to two different addresses setting the confirmation and comment:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 6 \"testing\"") +
            "\nSend two amounts to two different addresses, subtract fee from amount:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 1 \"\" \"[\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\",\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendmany", "\"\", \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\", 6, \"testing\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    std::string strAccount = AccountFromValue(request.params[0]);
    UniValue sendTo = request.params[1].get_obj();
    int nMinDepth = 1;
    if (!request.params[2].isNull())
        nMinDepth = request.params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (!request.params[3].isNull() && !request.params[3].get_str().empty())
        wtx.mapValue["comment"] = request.params[3].get_str();

    UniValue subtractFeeFromAmount(UniValue::VARR);
    if (!request.params[4].isNull())
        subtractFeeFromAmount = request.params[4].get_array();

    CCoinControl coin_control;
    if (!request.params[5].isNull()) {
        coin_control.signalRbf = request.params[5].get_bool();
    }

    if (!request.params[6].isNull()) {
        coin_control.m_confirm_target = ParseConfirmTarget(request.params[6]);
    }

    if (!request.params[7].isNull()) {
        if (!FeeModeFromString(request.params[7].get_str(), coin_control.m_fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
    }

    std::set<CTxDestination> destinations;
    std::vector<CRecipient> vecSend;

    CAmount totalAmount = 0;
    std::vector<std::string> keys = sendTo.getKeys();
    for (const std::string& name_ : keys) {
        CTxDestination dest = DecodeDestination(name_);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Merit address: ") + name_);
        }

        if (destinations.count(dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
        }
        destinations.insert(dest);

        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        bool fSubtractFeeFromAmount = false;
        for (unsigned int idx = 0; idx < subtractFeeFromAmount.size(); idx++) {
            const UniValue& addr = subtractFeeFromAmount[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked(pwallet);

    // Check funds
    CAmount nBalance = pwallet->GetLegacyBalance(ISMINE_SPENDABLE, nMinDepth, &strAccount);
    if (totalAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    // Send
    CReserveKey keyChange(pwallet);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    std::string strFailReason;
    bool fCreated = pwallet->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, coin_control);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, keyChange, g_connman.get(), state)) {
        strFailReason = strprintf("Transaction commit failed:: %s", state.GetRejectReason());
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    return wtx.GetHash().GetHex();
}

// Defined in rpc/misc.cpp
extern CScript _createmultisig_redeemScript(CWallet * const pwallet, const UniValue& params);

UniValue addmultisigaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
    {
        std::string msg = "addmultisigaddress nrequired [\"key\",...] ( \"account\" )\n"
            "\nAdd a nrequired-to-sign multisignature address to the wallet.\n"
            "Each key is a Merit address or hex-encoded public key.\n"
            "If 'account' is specified (DEPRECATED), assign address to that account.\n"

            "\nArguments:\n"
            "1. nrequired        (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"         (string, required) A json array of merit addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"address\"  (string) merit address or hex-encoded public key\n"
            "       ...,\n"
            "     ]\n"
            "2. \"script referral pubkey id\" (string) Pub key Id used to refer the script\n"
            "3. \"account\"      (string, optional) DEPRECATED. An account to assign the addresses to.\n"

            "\nResult:\n"
            "\"address\"         (string) A merit address associated with the keys.\n"

            "\nExamples:\n"
            "\nAdd a multisig address from 2 addresses\n"
            + HelpExampleCli("addmultisigaddress", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw std::runtime_error(msg);
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    uint160 scriptAddress;
    auto scriptDest = DecodeDestination(request.params[2].get_str());
    GetUint160(scriptDest, scriptAddress);

    std::string strAccount;
    if (!request.params[3].isNull())
        strAccount = AccountFromValue(request.params[3]);

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(pwallet, request.params);
    CScriptID innerID = inner;
    pwallet->AddCScript(inner, scriptAddress);

    pwallet->SetAddressBook(innerID, strAccount, "send");
    pwallet->SetAddressBook(scriptDest, strAccount, "send");
    return EncodeDestination(innerID);
}

class Witnessifier : public boost::static_visitor<bool>
{
public:
    CWallet * const pwallet;
    CScriptID result;

    explicit Witnessifier(CWallet *_pwallet) : pwallet(_pwallet) {}

    bool operator()(const CNoDestination &dest) const { return false; }

    bool operator()(const CKeyID &keyID) {
        if (pwallet) {
            CScript basescript = GetScriptForDestination(keyID);
            CScript witscript = GetScriptForWitness(basescript);
            SignatureData sigs;
            // This check is to make sure that the script we created can actually be solved for and signed by us
            // if we were to have the private keys. This is just to make sure that the script is valid and that,
            // if found in a transaction, we would still accept and relay that transaction.
            if (!ProduceSignature(DummySignatureCreator(pwallet), witscript, sigs) ||
                !VerifyScript(sigs.scriptSig, witscript, &sigs.scriptWitness, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, DummySignatureCreator(pwallet).Checker())) {
                return false;
            }
            result = CScriptID(witscript);
            pwallet->AddCScript(witscript, result);
            return true;
        }
        return false;
    }

    bool operator()(const CScriptID &scriptID) {
        CScript subscript;
        if (pwallet && pwallet->GetCScript(scriptID, subscript)) {
            int witnessversion;
            std::vector<unsigned char> witprog;
            if (subscript.IsWitnessProgram(witnessversion, witprog)) {
                result = scriptID;
                return true;
            }
            CScript witscript = GetScriptForWitness(subscript);
            SignatureData sigs;
            // This check is to make sure that the script we created can actually be solved for and signed by us
            // if we were to have the private keys. This is just to make sure that the script is valid and that,
            // if found in a transaction, we would still accept and relay that transaction.
            if (!ProduceSignature(DummySignatureCreator(pwallet), witscript, sigs) ||
                !VerifyScript(sigs.scriptSig, witscript, &sigs.scriptWitness, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, DummySignatureCreator(pwallet).Checker())) {
                return false;
            }
            result = CScriptID(witscript);
            pwallet->AddCScript(witscript, result);
            return true;
        }
        return false;
    }

    bool operator()(const CParamScriptID &scriptID) {
        CScript subscript;
        if (pwallet && pwallet->GetParamScript(scriptID, subscript)) {
            int witnessversion;
            std::vector<unsigned char> witprog;
            //Parameterized scripts cannot be witness programs
            if (subscript.IsWitnessProgram(witnessversion, witprog)) {
                return false;
            }
            CScript witscript = GetScriptForWitness(subscript);
            SignatureData sigs;
            // This check is to make sure that the script we created can actually be solved for and signed by us
            // if we were to have the private keys. This is just to make sure that the script is valid and that,
            // if found in a transaction, we would still accept and relay that transaction.
            if (!ProduceSignature(DummySignatureCreator(pwallet), witscript, sigs) ||
                !VerifyScript(sigs.scriptSig, witscript, &sigs.scriptWitness, MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, DummySignatureCreator(pwallet).Checker())) {
                return false;
            }
            result = CScriptID(witscript);
            pwallet->AddCScript(witscript,  result);
            return true;
        }
        return false;
    }
};

UniValue addwitnessaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1)
    {
        std::string msg = "addwitnessaddress \"address\"\n"
            "\nAdd a witness address for a script (with pubkey or redeemscript known).\n"
            "It returns the witness script.\n"

            "\nArguments:\n"
            "1. \"address\"       (string, required) An address known to the wallet\n"

            "\nResult:\n"
            "\"witnessaddress\",  (string) The value of the new address (P2SH of witness script).\n"
            "}\n"
        ;
        throw std::runtime_error(msg);
    }

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Merit address");
    }

    Witnessifier w(pwallet);
    bool ret = boost::apply_visitor(w, dest);
    if (!ret) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Public key or redeemscript not known to wallet, or the key is uncompressed");
    }

    pwallet->SetAddressBook(w.result, "", "receive");

    return EncodeDestination(w.result);
}

struct tallyitem
{
    CAmount nAmount;
    int nConf;
    std::vector<uint256> txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

UniValue ListReceived(CWallet * const pwallet, const UniValue& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (!params[0].isNull())
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (!params[1].isNull())
        fIncludeEmpty = params[1].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;
    if(!params[2].isNull())
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    // Tally
    std::map<CTxDestination, tallyitem> mapTally;
    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;

        if (wtx.IsCoinBase() || !CheckFinalTx(*wtx.tx))
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        for (const CTxOut& txout : wtx.tx->vout)
        {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            isminefilter mine = IsMine(*pwallet, address);
            if(!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    std::map<std::string, tallyitem> mapAccountTally;
    for (const std::pair<CTxDestination, CAddressBookData>& item : pwallet->mapAddressBook) {
        const CTxDestination& dest = item.first;
        const std::string& strAccount = item.second.name;
        std::map<CTxDestination, tallyitem>::iterator it = mapTally.find(dest);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (fByAccounts)
        {
            tallyitem& _item = mapAccountTally[strAccount];
            _item.nAmount += nAmount;
            _item.nConf = std::min(_item.nConf, nConf);
            _item.fIsWatchonly = fIsWatchonly;
        }
        else
        {
            UniValue obj(UniValue::VOBJ);
            if(fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("address",       EncodeDestination(dest)));
            obj.push_back(Pair("account",       strAccount));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            if (!fByAccounts)
                obj.push_back(Pair("label", strAccount));
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end())
            {
                for (const uint256& _item : (*it).second.txids)
                {
                    transactions.push_back(_item.GetHex());
                }
            }
            obj.push_back(Pair("txids", transactions));
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (std::map<std::string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            CAmount nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            UniValue obj(UniValue::VOBJ);
            if((*it).second.fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("account",       (*it).first));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue listreceivedbyaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "listreceivedbyaddress ( minconf include_empty include_watchonly)\n"
            "\nList balances by receiving address.\n"
            "\nArguments:\n"
            "1. minconf           (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. include_empty     (bool, optional, default=false) Whether to include addresses that haven't received any payments.\n"
            "3. include_watchonly (bool, optional, default=false) Whether to include watch-only addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,        (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
            "    \"account\" : \"accountname\",       (string) DEPRECATED. The account of the receiving address. The default account is \"\".\n"
            "    \"amount\" : x.xxx,                  (numeric) The total amount in " + CURRENCY_UNIT + " received by the address\n"
            "    \"confirmations\" : n,               (numeric) The number of confirmations of the most recent transaction included\n"
            "    \"label\" : \"label\",               (string) A comment for the address/transaction, if any\n"
            "    \"txids\": [\n"
            "       n,                                (numeric) The ids of transactions received with the address \n"
            "       ...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaddress", "")
            + HelpExampleCli("listreceivedbyaddress", "6 true")
            + HelpExampleRpc("listreceivedbyaddress", "6, true, true")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, false);
}

UniValue listreceivedbyaccount(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "listreceivedbyaccount ( minconf include_empty include_watchonly)\n"
            "\nDEPRECATED. List balances by account.\n"
            "\nArguments:\n"
            "1. minconf           (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. include_empty     (bool, optional, default=false) Whether to include accounts that haven't received any payments.\n"
            "3. include_watchonly (bool, optional, default=false) Whether to include watch-only addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,   (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"account\" : \"accountname\",  (string) The account name of the receiving account\n"
            "    \"amount\" : x.xxx,             (numeric) The total amount received by addresses with this account\n"
            "    \"confirmations\" : n,          (numeric) The number of confirmations of the most recent transaction included\n"
            "    \"label\" : \"label\"           (string) A comment for the address/transaction, if any\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaccount", "")
            + HelpExampleCli("listreceivedbyaccount", "6 true")
            + HelpExampleRpc("listreceivedbyaccount", "6, true, true")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    return ListReceived(pwallet, request.params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    if (IsValidDestination(dest)) {
        entry.push_back(Pair("address", EncodeDestination(dest)));
    }
}

/**
 * List transactions based on the given criteria.
 *
 * @param  pwallet    The wallet.
 * @param  wtx        The wallet transaction.
 * @param  strAccount The account, if any, or "*" for all.
 * @param  nMinDepth  The minimum confirmation depth.
 * @param  fLong      Whether to include the JSON version of the transaction.
 * @param  ret        The UniValue into which the result is stored.
 * @param  filter     The "is mine" filter bool.
 */
void ListTransactions(CWallet* const pwallet, const CWalletTx& wtx, const std::string& strAccount, int nMinDepth, bool fLong, UniValue& ret, const isminefilter& filter)
{
    CAmount nFee;
    std::string strSentAccount;
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;

    wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, filter);

    bool fAllAccounts = (strAccount == std::string("*"));
    bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

    // Sent
    if ((!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        for (const COutputEntry& s : listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (::IsMine(*pwallet, s.destination) & ISMINE_WATCH_ONLY)) {
                entry.push_back(Pair("involvesWatchonly", true));
            }
            entry.push_back(Pair("account", strSentAccount));
            MaybePushAddress(entry, s.destination);
            entry.push_back(Pair("category", "send"));
            entry.push_back(Pair("amount", ValueFromAmount(-s.amount)));
            if (pwallet->mapAddressBook.count(s.destination)) {
                entry.push_back(Pair("label", pwallet->mapAddressBook[s.destination].name));
            }
            entry.push_back(Pair("vout", s.vout));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            entry.push_back(Pair("abandoned", wtx.isAbandoned()));
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
    {
        for (const COutputEntry& r : listReceived)
        {
            std::string account;
            if (pwallet->mapAddressBook.count(r.destination)) {
                account = pwallet->mapAddressBook[r.destination].name;
            }
            if (fAllAccounts || (account == strAccount))
            {
                UniValue entry(UniValue::VOBJ);
                if (involvesWatchonly || (::IsMine(*pwallet, r.destination) & ISMINE_WATCH_ONLY)) {
                    entry.push_back(Pair("involvesWatchonly", true));
                }
                entry.push_back(Pair("account", account));
                MaybePushAddress(entry, r.destination);
                if (wtx.IsCoinBase())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry.push_back(Pair("category", "orphan"));
                    else if (wtx.GetBlocksToMaturity() > 0)
                        entry.push_back(Pair("category", "immature"));
                    else
                        entry.push_back(Pair("category", "generate"));
                }
                else
                {
                    entry.push_back(Pair("category", "receive"));
                }
                entry.push_back(Pair("amount", ValueFromAmount(r.amount)));
                if (pwallet->mapAddressBook.count(r.destination)) {
                    entry.push_back(Pair("label", account));
                }
                entry.push_back(Pair("vout", r.vout));
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
        }
    }
}

void AcentryToJSON(const CAccountingEntry& acentry, const std::string& strAccount, UniValue& ret)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("account", acentry.strAccount));
        entry.push_back(Pair("category", "move"));
        entry.push_back(Pair("time", acentry.nTime));
        entry.push_back(Pair("amount", ValueFromAmount(acentry.nCreditDebit)));
        entry.push_back(Pair("otheraccount", acentry.strOtherAccount));
        entry.push_back(Pair("comment", acentry.strComment));
        ret.push_back(entry);
    }
}

UniValue listtransactions(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 4)
        throw std::runtime_error(
            "listtransactions ( \"account\" count skip include_watchonly)\n"
            "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.\n"
            "\nArguments:\n"
            "1. \"account\"    (string, optional) DEPRECATED. The account name. Should be \"*\".\n"
            "2. count          (numeric, optional, default=10) The number of transactions to return\n"
            "3. skip           (numeric, optional, default=0) The number of transactions to skip\n"
            "4. include_watchonly (bool, optional, default=false) Include transactions to watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. \n"
            "                                                It will be \"\" for the default account.\n"
            "    \"address\":\"address\",    (string) The merit address of the transaction. Not present for \n"
            "                                                move transactions (category = move).\n"
            "    \"category\":\"send|receive|move\", (string) The transaction category. 'move' is a local (off blockchain)\n"
            "                                                transaction between accounts, and not associated with an address,\n"
            "                                                transaction id or block. 'send' and 'receive' transactions are \n"
            "                                                associated with an address, transaction id and block details\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the\n"
            "                                         'move' category for moves outbound. It is positive for the 'receive' category,\n"
            "                                         and for the 'move' category for inbound funds.\n"
            "    \"label\": \"label\",       (string) A comment for the address/transaction, if any\n"
            "    \"vout\": n,                (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                                         'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and \n"
            "                                         'receive' category of transactions. Negative confirmations indicate the\n"
            "                                         transaction conflicts with the block chain\n"
            "    \"trusted\": xxx,           (bool) Whether we consider the outputs of this unconfirmed transaction safe to spend.\n"
            "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\", (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available \n"
            "                                          for 'send' and 'receive' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"otheraccount\": \"accountname\",  (string) DEPRECATED. For the 'move' category of transactions, the account the funds came \n"
            "                                          from (for receiving funds, positive amounts), or went to (for sending funds,\n"
            "                                          negative amounts).\n"
            "    \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                     may be unknown for unconfirmed transactions not in the mempool\n"
            "    \"abandoned\": xxx          (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
            "                                         'send' category of transactions.\n"
            "  }\n"
            "]\n"

            "\nExamples:\n"
            "\nList the most recent 10 transactions in the systems\n"
            + HelpExampleCli("listtransactions", "") +
            "\nList transactions 100 to 120\n"
            + HelpExampleCli("listtransactions", "\"*\" 20 100") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAccount = "*";
    if (!request.params[0].isNull())
        strAccount = request.params[0].get_str();
    int nCount = 10;
    if (!request.params[1].isNull())
        nCount = request.params[1].get_int();
    int nFrom = 0;
    if (!request.params[2].isNull())
        nFrom = request.params[2].get_int();
    isminefilter filter = ISMINE_SPENDABLE;
    if(!request.params[3].isNull())
        if(request.params[3].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    UniValue ret(UniValue::VARR);

    const CWallet::TxItems & txOrdered = pwallet->wtxOrdered;

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != nullptr)
            ListTransactions(pwallet, *pwtx, strAccount, 0, true, ret, filter);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != nullptr)
            AcentryToJSON(*pacentry, strAccount, ret);

        if ((int)ret.size() >= (nCount+nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    std::vector<UniValue> arrTmp = ret.getValues();

    std::vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    std::vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom+nCount);

    if (last != arrTmp.end()) arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin()) arrTmp.erase(arrTmp.begin(), first);

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

UniValue listaccounts(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "listaccounts ( minconf include_watchonly)\n"
            "\nDEPRECATED. Returns Object that has account names as keys, account balances as values.\n"
            "\nArguments:\n"
            "1. minconf             (numeric, optional, default=1) Only include transactions with at least this many confirmations\n"
            "2. include_watchonly   (bool, optional, default=false) Include balances in watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "{                      (json object where keys are account names, and values are numeric balances\n"
            "  \"account\": x.xxx,  (numeric) The property name is the account name, and the value is the total balance for the account.\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n"
            "\nList account balances where there at least 1 confirmation\n"
            + HelpExampleCli("listaccounts", "") +
            "\nList account balances including zero confirmation transactions\n"
            + HelpExampleCli("listaccounts", "0") +
            "\nList account balances for 6 or more confirmations\n"
            + HelpExampleCli("listaccounts", "6") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("listaccounts", "6")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    int nMinDepth = 1;
    if (!request.params[0].isNull())
        nMinDepth = request.params[0].get_int();
    isminefilter includeWatchonly = ISMINE_SPENDABLE;
    if(!request.params[1].isNull())
        if(request.params[1].get_bool())
            includeWatchonly = includeWatchonly | ISMINE_WATCH_ONLY;

    std::map<std::string, CAmount> mapAccountBalances;
    for (const std::pair<CTxDestination, CAddressBookData>& entry : pwallet->mapAddressBook) {
        if (IsMine(*pwallet, entry.first) & includeWatchonly) {  // This address belongs to me
            mapAccountBalances[entry.second.name] = 0;
        }
    }

    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        CAmount nFee;
        std::string strSentAccount;
        std::list<COutputEntry> listReceived;
        std::list<COutputEntry> listSent;
        int nDepth = wtx.GetDepthInMainChain();
        if (wtx.GetBlocksToMaturity() > 0 || nDepth < 0)
            continue;
        wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, includeWatchonly);
        mapAccountBalances[strSentAccount] -= nFee;
        for (const COutputEntry& s : listSent)
            mapAccountBalances[strSentAccount] -= s.amount;
        if (nDepth >= nMinDepth)
        {
            for (const COutputEntry& r : listReceived)
                if (pwallet->mapAddressBook.count(r.destination)) {
                    mapAccountBalances[pwallet->mapAddressBook[r.destination].name] += r.amount;
                }
                else
                    mapAccountBalances[""] += r.amount;
        }
    }

    const std::list<CAccountingEntry>& acentries = pwallet->laccentries;
    for (const CAccountingEntry& entry : acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    UniValue ret(UniValue::VOBJ);
    for (const std::pair<std::string, CAmount>& accountBalance : mapAccountBalances) {
        ret.push_back(Pair(accountBalance.first, ValueFromAmount(accountBalance.second)));
    }
    return ret;
}

UniValue listsinceblock(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 4)
        throw std::runtime_error(
            "listsinceblock ( \"blockhash\" target_confirmations include_watchonly include_removed )\n"
            "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted.\n"
            "If \"blockhash\" is no longer a part of the main chain, transactions from the fork point onward are included.\n"
            "Additionally, if include_removed is set, transactions affecting the wallet which were removed are returned in the \"removed\" array.\n"
            "\nArguments:\n"
            "1. \"blockhash\"            (string, optional) The block hash to list transactions since\n"
            "2. target_confirmations:    (numeric, optional, default=1) Return the nth block hash from the main chain. e.g. 1 would mean the best block hash. Note: this is not used as a filter, but only affects [lastblock] in the return value\n"
            "3. include_watchonly:       (bool, optional, default=false) Include transactions to watch-only addresses (see 'importaddress')\n"
            "4. include_removed:         (bool, optional, default=true) Show transactions that were removed due to a reorg in the \"removed\" array\n"
            "                                                           (not guaranteed to work on pruned nodes)\n"
            "\nResult:\n"
            "{\n"
            "  \"transactions\": [\n"
            "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. Will be \"\" for the default account.\n"
            "    \"address\":\"address\",    (string) The merit address of the transaction. Not present for move transactions (category = move).\n"
            "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the 'move' category for moves \n"
            "                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.\n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the 'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "                                          When it's < 0, it means the transaction conflicted that many blocks ago.\n"
            "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.\n"
            "    \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                   may be unknown for unconfirmed transactions not in the mempool\n"
            "    \"abandoned\": xxx,         (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the 'send' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"label\" : \"label\"       (string) A comment for the address/transaction, if any\n"
            "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
            "  ],\n"
            "  \"removed\": [\n"
            "    <structure is the same as \"transactions\" above, only present if include_removed=true>\n"
            "    Note: transactions that were readded in the active chain will appear as-is in this array, and may thus have a positive confirmation count.\n"
            "  ],\n"
            "  \"lastblock\": \"lastblockhash\"     (string) The hash of the block (target_confirmations-1) from the best block on the main chain. This is typically used to feed back into listsinceblock the next time you call it. So you would generally use a target_confirmations of say 6, so you will be continually re-notified of transactions until they've reached 6 confirmations plus any new ones\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listsinceblock", "")
            + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
            + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    const CBlockIndex* pindex = nullptr;    // Block index of the specified block or the common ancestor, if the block provided was in a deactivated chain.
    const CBlockIndex* paltindex = nullptr; // Block index of the specified block, even if it's in a deactivated chain.
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    if (!request.params[0].isNull()) {
        uint256 blockId;

        blockId.SetHex(request.params[0].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end()) {
            paltindex = pindex = it->second;
            if (chainActive[pindex->nHeight] != pindex) {
                // the block being asked for is a part of a deactivated chain;
                // we don't want to depend on its perceived height in the block
                // chain, we want to instead use the last common ancestor
                pindex = chainActive.FindFork(pindex);
            }
        }
    }

    if (!request.params[1].isNull()) {
        target_confirms = request.params[1].get_int();

        if (target_confirms < 1) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }
    }

    if (!request.params[2].isNull() && request.params[2].get_bool()) {
        filter = filter | ISMINE_WATCH_ONLY;
    }

    bool include_removed = (request.params[3].isNull() || request.params[3].get_bool());

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

    for (const std::pair<uint256, CWalletTx>& pairWtx : pwallet->mapWallet) {
        CWalletTx tx = pairWtx.second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth) {
            ListTransactions(pwallet, tx, "*", 0, true, transactions, filter);
        }
    }

    // when a reorg'd block is requested, we also list any relevant transactions
    // in the blocks of the chain that was detached
    UniValue removed(UniValue::VARR);
    while (include_removed && paltindex && paltindex != pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, paltindex, Params().GetConsensus())) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }
        for (const CTransactionRef& tx : block.vtx) {
            auto it = pwallet->mapWallet.find(tx->GetHash());
            if (it != pwallet->mapWallet.end()) {
                // We want all transactions regardless of confirmation count to appear here,
                // even negative confirmation ones, hence the big negative.
                ListTransactions(pwallet, it->second, "*", -100000000, true, removed, filter);
            }
        }
        paltindex = paltindex->pprev;
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("transactions", transactions));
    if (include_removed) ret.push_back(Pair("removed", removed));
    ret.push_back(Pair("lastblock", lastblock.GetHex()));

    return ret;
}

UniValue gettransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "gettransaction \"txid\" ( include_watchonly )\n"
            "\nGet detailed information about in-wallet transaction <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"                  (string, required) The transaction id\n"
            "2. \"include_watchonly\"     (bool, optional, default=false) Whether to include watch-only addresses in balance calculation and details[]\n"
            "\nResult:\n"
            "{\n"
            "  \"amount\" : x.xxx,        (numeric) The transaction amount in " + CURRENCY_UNIT + "\n"
            "  \"fee\": x.xxx,            (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                              'send' category of transactions.\n"
            "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
            "  \"blockhash\" : \"hash\",  (string) The block hash\n"
            "  \"blockindex\" : xx,       (numeric) The index of the transaction in the block that includes it\n"
            "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id.\n"
            "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"bip125-replaceable\": \"yes|no|unknown\",  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
            "                                                   may be unknown for unconfirmed transactions not in the mempool\n"
            "  \"details\" : [\n"
            "    {\n"
            "      \"account\" : \"accountname\",      (string) DEPRECATED. The account name involved in the transaction, can be \"\" for the default account.\n"
            "      \"address\" : \"address\",          (string) The merit address involved in the transaction\n"
            "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
            "      \"amount\" : x.xxx,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
            "      \"label\" : \"label\",              (string) A comment for the address/transaction, if any\n"
            "      \"vout\" : n,                       (numeric) the vout value\n"
            "      \"fee\": x.xxx,                     (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                                           'send' category of transactions.\n"
            "      \"abandoned\": xxx                  (bool) 'true' if the transaction has been abandoned (inputs are respendable). Only available for the \n"
            "                                           'send' category of transactions.\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
            + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    isminefilter filter = ISMINE_SPENDABLE;
    if(!request.params[1].isNull())
        if(request.params[1].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    UniValue entry(UniValue::VOBJ);
    auto it = pwallet->mapWallet.find(hash);
    if (it == pwallet->mapWallet.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    const CWalletTx& wtx = it->second;

    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? wtx.tx->GetValueOut() - nDebit : 0);

    entry.push_back(Pair("amount", ValueFromAmount(nNet - nFee)));
    if (wtx.IsFromMe(filter))
        entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(pwallet, wtx, "*", 0, false, details, filter);
    entry.push_back(Pair("details", details));

    std::string strHex = EncodeHexTx(static_cast<CTransaction>(wtx), RPCSerializationFlags());
    entry.push_back(Pair("hex", strHex));

    return entry;
}

UniValue abandontransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "abandontransaction \"txid\"\n"
            "\nMark in-wallet transaction <txid> as abandoned\n"
            "This will mark this transaction and all its in-wallet descendants as abandoned which will allow\n"
            "for their inputs to be respent.  It can be used to replace \"stuck\" or evicted transactions.\n"
            "It only works on transactions which are not included in a block and are not currently in the mempool.\n"
            "It has no effect on transactions which are already conflicted or abandoned.\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    if (!pwallet->mapWallet.count(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    if (!pwallet->AbandonTransaction(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
    }

    return NullUniValue;
}


UniValue backupwallet(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "backupwallet \"destination\"\n"
            "\nSafely copies current wallet file to destination, which can be a directory or a path with filename.\n"
            "\nArguments:\n"
            "1. \"destination\"   (string) The destination directory or file\n"
            "\nExamples:\n"
            + HelpExampleCli("backupwallet", "\"backup.dat\"")
            + HelpExampleRpc("backupwallet", "\"backup.dat\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strDest = request.params[0].get_str();
    if (!pwallet->BackupWallet(strDest)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");
    }

    return NullUniValue;
}


UniValue keypoolrefill(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "keypoolrefill ( newsize )\n"
            "\nFills the keypool."
            + HelpRequiringPassphrase(pwallet) + "\n"
            "\nArguments\n"
            "1. newsize     (numeric, optional, default=100) The new keypool size\n"
            "\nExamples:\n"
            + HelpExampleCli("keypoolrefill", "")
            + HelpExampleRpc("keypoolrefill", "")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (!request.params[0].isNull()) {
        if (request.params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)request.params[0].get_int();
    }

    EnsureWalletIsUnlocked(pwallet);
    pwallet->TopUpKeyPool(kpSize);

    if (pwallet->GetKeyPoolSize() < kpSize) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");
    }

    return NullUniValue;
}


static void LockWallet(CWallet* pWallet)
{
    LOCK(pWallet->cs_wallet);
    pWallet->nRelockTime = 0;
    pWallet->Lock();
}

UniValue walletpassphrase(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 2)) {
        throw std::runtime_error(
            "walletpassphrase \"passphrase\" timeout\n"
            "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
            "This is needed prior to performing transactions related to private keys such as sending merits\n"
            "\nArguments:\n"
            "1. \"passphrase\"     (string, required) The wallet passphrase\n"
            "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
            "\nNote:\n"
            "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
            "time that overrides the old one.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 60 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
            "\nLock the wallet again (before 60 seconds)\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");
    }

    // Note that the walletpassphrase is stored in request.params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    strWalletPass = request.params[0].get_str().c_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwallet->Unlock(strWalletPass)) {
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
        }
    }
    else
        throw std::runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    pwallet->TopUpKeyPool();

    int64_t nSleepTime = request.params[1].get_int64();
    pwallet->nRelockTime = GetTime() + nSleepTime;
    RPCRunLater(strprintf("lockwallet(%s)", pwallet->GetName()), boost::bind(LockWallet, pwallet), nSleepTime);

    return NullUniValue;
}


UniValue walletpassphrasechange(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 2)) {
        throw std::runtime_error(
            "walletpassphrasechange \"oldpassphrase\" \"newpassphrase\"\n"
            "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
            "\nArguments:\n"
            "1. \"oldpassphrase\"      (string) The current passphrase\n"
            "2. \"newpassphrase\"      (string) The new passphrase\n"
            "\nExamples:\n"
            + HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
            + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");
    }

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = request.params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = request.params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw std::runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwallet->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass)) {
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }

    return NullUniValue;
}


UniValue walletlock(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsCrypted() && (request.fHelp || request.params.size() != 0)) {
        throw std::runtime_error(
            "walletlock\n"
            "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.\n"
            "\nExamples:\n"
            "\nSet the passphrase for 2 minutes to perform a transaction\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
            "\nPerform a send (requires passphrase set)\n"
            + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 1.0") +
            "\nClear the passphrase since we are done before 2 minutes is up\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletlock", "")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (!pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");
    }

    pwallet->Lock();
    pwallet->nRelockTime = 0;

    return NullUniValue;
}


UniValue encryptwallet(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (!pwallet->IsCrypted() && (request.fHelp || request.params.size() != 1)) {
        throw std::runtime_error(
            "encryptwallet \"passphrase\"\n"
            "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
            "After this, any calls that interact with private keys such as sending or signing \n"
            "will require the passphrase to be set prior the making these calls.\n"
            "Use the walletpassphrase call for this, and then walletlock call.\n"
            "If the wallet is already encrypted, use the walletpassphrasechange call.\n"
            "Note that this will shutdown the server.\n"
            "\nArguments:\n"
            "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.\n"
            "\nExamples:\n"
            "\nEncrypt your wallet\n"
            + HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
            "\nNow set the passphrase to use the wallet, such as for signing or sending merit\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
            "\nNow we can do something like sign\n"
            + HelpExampleCli("signmessage", "\"address\" \"test message\"") +
            "\nNow lock the wallet again by removing the passphrase\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    if (request.fHelp)
        return true;
    if (pwallet->IsCrypted()) {
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");
    }

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make request.params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = request.params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw std::runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwallet->EncryptWallet(strWalletPass)) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");
    }

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return "wallet encrypted; Merit server stopping, restart to run with encrypted wallet. The keypool has been flushed and a new HD seed was generated (if you are using HD). You need to make a new backup.";
}

UniValue lockunspent(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "lockunspent unlock ([{\"txid\":\"txid\",\"vout\":n},...])\n"
            "\nUpdates list of temporarily unspendable outputs.\n"
            "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
            "If no transaction outputs are specified when unlocking then all current locked transaction outputs are unlocked.\n"
            "A locked transaction output will not be chosen by automatic coin selection, when spending merits.\n"
            "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
            "is always cleared (by virtue of process exit) when a node stops or fails.\n"
            "Also see the listunspent call\n"
            "\nArguments:\n"
            "1. unlock            (boolean, required) Whether to unlock (true) or lock (false) the specified transactions\n"
            "2. \"transactions\"  (string, optional) A json array of objects. Each object the txid (string) vout (numeric)\n"
            "     [           (json array of json objects)\n"
            "       {\n"
            "         \"txid\":\"id\",    (string) The transaction id\n"
            "         \"vout\": n         (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "true|false    (boolean) Whether the command was successful or not\n"

            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    RPCTypeCheckArgument(request.params[0], UniValue::VBOOL);

    bool fUnlock = request.params[0].get_bool();

    if (request.params[1].isNull()) {
        if (fUnlock)
            pwallet->UnlockAllCoins();
        return true;
    }

    RPCTypeCheckArgument(request.params[1], UniValue::VARR);

    UniValue outputs = request.params[1].get_array();
    for (unsigned int idx = 0; idx < outputs.size(); idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o,
            {
                {"txid", UniValueType(UniValue::VSTR)},
                {"vout", UniValueType(UniValue::VNUM)},
            });

        std::string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);

        if (fUnlock)
            pwallet->UnlockCoin(outpt);
        else
            pwallet->LockCoin(outpt);
    }

    return true;
}

UniValue listlockunspent(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "listlockunspent\n"
            "\nReturns list of temporarily unspendable outputs.\n"
            "See the lockunspent call to lock and unlock transactions for spending.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\" : \"transactionid\",     (string) The transaction id locked\n"
            "    \"vout\" : n                      (numeric) The vout value\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listlockunspent", "")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<COutPoint> vOutpts;
    pwallet->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    for (COutPoint &outpt : vOutpts) {
        UniValue o(UniValue::VOBJ);

        o.push_back(Pair("txid", outpt.hash.GetHex()));
        o.push_back(Pair("vout", (int)outpt.n));
        ret.push_back(o);
    }

    return ret;
}

UniValue settxfee(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1)
        throw std::runtime_error(
            "settxfee amount\n"
            "\nSet the transaction fee per kB. Overwrites the paytxfee parameter.\n"
            "\nArguments:\n"
            "1. amount         (numeric or string, required) The transaction fee in " + CURRENCY_UNIT + "/kB\n"
            "\nResult\n"
            "true|false        (boolean) Returns true if successful\n"
            "\nExamples:\n"
            + HelpExampleCli("settxfee", "0.00001")
            + HelpExampleRpc("settxfee", "0.00001")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // Amount
    CAmount nAmount = AmountFromValue(request.params[0]);

    payTxFee = CFeeRate(nAmount, 1000);
    return true;
}

UniValue getwalletinfo(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getwalletinfo\n"
            "Returns an object containing various wallet state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"walletname\": xxxxx,             (string) the wallet name\n"
            "  \"walletversion\": xxxxx,          (numeric) the wallet version\n"
            "  \"tag\": xxxxx,                    (string, optional) the wallet tag\n"
            "  \"balance\": xxxxxxx,              (numeric) the total confirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"unconfirmed_balance\": xxx,      (numeric) the total unconfirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"immature_balance\": xxxxxx,      (numeric) the total immature balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"txcount\": xxxxxxx,              (numeric) the total number of transactions in the wallet\n"
            "  \"keypoololdest\": xxxxxx,         (numeric) the timestamp (seconds since Unix epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,             (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,           (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,              (numeric) the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB\n"
            "  \"hdmasterkeyid\": \"<hash160>\"   (string) the Hash160 of the HD master pubkey\n"
            "  \"referred\": true|false           (boolean) if wallet is referred\n"
            "  \"referraladdress\": xxxxxx        (string) referral address to use to share with other users\n"
            "  \"invites\": xxxxxx                (numeric) number of available invites\n"
            "  \"immature_invites\": xxxxxx       (numeric) number of immature invites\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);

    obj.push_back(Pair("walletname", pwallet->GetName()));
    obj.push_back(Pair("walletversion", pwallet->GetVersion()));
    obj.push_back(Pair("tag", pwallet->GetTag()));
    obj.push_back(Pair("balance",       ValueFromAmount(pwallet->GetBalance())));
    obj.push_back(Pair("unconfirmed_balance", ValueFromAmount(pwallet->GetUnconfirmedBalance())));
    obj.push_back(Pair("immature_balance",    ValueFromAmount(pwallet->GetImmatureBalance())));
    obj.push_back(Pair("txcount",       (int)pwallet->mapWallet.size()));
    obj.push_back(Pair("keypoololdest", pwallet->GetOldestKeyPoolTime()));
    obj.push_back(Pair("keypoolsize", (int64_t)pwallet->GetKeyPoolSize()));
    if (pwallet->IsCrypted()) {
        obj.push_back(Pair("unlocked_until", pwallet->nRelockTime));
    }
    obj.push_back(Pair("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK())));

    CKeyID masterKeyID = pwallet->GetHDChain().masterKeyID;
    if (!masterKeyID.IsNull()) {
         obj.push_back(Pair("hdmasterkeyid", masterKeyID.GetHex()));
    }

    if (!pwallet->IsReferred()) {
        obj.push_back(Pair("referred", false));
    } else {
        obj.push_back(Pair("referred", true));
        auto referral = pwallet->GetRootReferral();
        assert(!referral->GetHash().IsNull());

        obj.push_back(Pair("referraladdress", EncodeDestination(CKeyID{referral->GetAddress()})));
    }

    obj.push_back(Pair("invites", pwallet->GetAvailableBalance(nullptr, true)));
    obj.push_back(Pair("immature_invites", pwallet->GetImmatureBalance(true)));

    return obj;
}

UniValue listwallets(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "listwallets\n"
            "Returns a list of currently loaded wallets.\n"
            "For full information on the wallet, use \"getwalletinfo\"\n"
            "\nResult:\n"
            "[                         (json array of strings)\n"
            "  \"walletname\"            (string) the wallet name\n"
            "   ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listwallets", "")
            + HelpExampleRpc("listwallets", "")
        );

    UniValue obj(UniValue::VARR);

    for (CWalletRef pwallet : vpwallets) {

        if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
            return NullUniValue;
        }

        LOCK(pwallet->cs_wallet);

        obj.push_back(pwallet->GetName());
    }

    return obj;
}

UniValue resendwallettransactions(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "resendwallettransactions\n"
            "Immediately re-broadcast unconfirmed wallet transactions to all peers.\n"
            "Intended only for testing; the wallet code periodically re-broadcasts\n"
            "automatically.\n"
            "Returns an RPC error if -walletbroadcast is set to false.\n"
            "Returns array of transaction ids that were re-broadcast.\n"
            );

    if (!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    LOCK2(cs_main, pwallet->cs_wallet);

    if (!pwallet->GetBroadcastTransactions()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet transaction broadcasting is disabled with -walletbroadcast");
    }

    std::vector<uint256> txids = pwallet->ResendWalletTransactionsBefore(GetTime(), g_connman.get());
    UniValue result(UniValue::VARR);
    for (const uint256& txid : txids)
    {
        result.push_back(txid.ToString());
    }
    return result;
}

UniValue listunspent(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 5)
        throw std::runtime_error(
            "listunspent ( minconf maxconf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"      (string) A json array of merit addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) merit address\n"
            "      ,...\n"
            "    ]\n"
            "4. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "5. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " + CURRENCY_UNIT + "\n"
            "    }\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",          (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the merit address\n"
            "    \"account\" : \"account\",    (string) DEPRECATED. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listunspent", "")
            + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleCli("listunspent", "6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
            + HelpExampleRpc("listunspent", "6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        );

    ObserveSafeMode();

    int nMinDepth = 1;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
        nMinDepth = request.params[0].get_int();
    }

    int nMaxDepth = 9999999;
    if (!request.params[1].isNull()) {
        RPCTypeCheckArgument(request.params[1], UniValue::VNUM);
        nMaxDepth = request.params[1].get_int();
    }

    std::set<CTxDestination> destinations;
    if (!request.params[2].isNull()) {
        RPCTypeCheckArgument(request.params[2], UniValue::VARR);
        UniValue inputs = request.params[2].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Merit address: ") + input.get_str());
            }
            if (!destinations.insert(dest).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + input.get_str());
            }
        }
    }

    bool include_unsafe = true;
    if (!request.params[3].isNull()) {
        RPCTypeCheckArgument(request.params[3], UniValue::VBOOL);
        include_unsafe = request.params[3].get_bool();
    }

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;

    if (!request.params[4].isNull()) {
        const UniValue& options = request.params[4].get_obj();

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();
    }

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    assert(pwallet != nullptr);
    LOCK2(cs_main, pwallet->cs_wallet);

    pwallet->AvailableCoins(
            vecOutputs,
            !include_unsafe,
            nullptr,
            nMinimumAmount,
            nMaximumAmount,
            nMinimumSumAmount,
            nMaximumCount,
            nMinDepth,
            nMaxDepth);

    for (const COutput& out : vecOutputs) {
        CTxDestination address;
        const CScript& scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
        bool fValidAddress = ExtractDestination(scriptPubKey, address);

        if (destinations.size() && (!fValidAddress || !destinations.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));

        if (fValidAddress) {
            entry.push_back(Pair("address", EncodeDestination(address)));

            if (pwallet->mapAddressBook.count(address)) {
                entry.push_back(
                        Pair("account", pwallet->mapAddressBook[address].name));
            }

            CScript redeemScript;
            if (scriptPubKey.IsPayToScriptHash()) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                pwallet->GetCScript(hash, redeemScript);
            } else if (scriptPubKey.IsParameterizedPayToScriptHash()) {
                const CParamScriptID& hash = boost::get<CParamScriptID>(address);
                pwallet->GetParamScript(hash, redeemScript);
            }

            if (!redeemScript.empty()) {
                entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }

        entry.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
        entry.push_back(Pair("amount", ValueFromAmount(out.tx->tx->vout[out.i].nValue)));
        entry.push_back(Pair("confirmations", out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable));
        entry.push_back(Pair("solvable", out.fSolvable));
        entry.push_back(Pair("safe", out.fSafe));
        results.push_back(entry);
    }

    return results;
}

UniValue listinvites(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 5)
        throw std::runtime_error(
            "listinvites ( [\"addresses\",...] )\n"
            "\nReturns array of unspent invite outputs\n"
            "Optionally filter specified addresses.\n"
            "\nArguments:\n"
            "1. \"addresses\"      (string) A json array of merit addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) merit address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"id\" : \"id\",          (string) the invite id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the merit address\n"
            "    \"account\" : \"account\",    (string) DEPRECATED. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) amount of invites\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listinvites", "")
            + HelpExampleCli("listinvites", "\"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
        );

    ObserveSafeMode();

    std::set<CTxDestination> destinations;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VARR);
        UniValue inputs = request.params[0].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Merit address: ") + input.get_str());
            }
            if (!destinations.insert(dest).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + input.get_str());
            }
        }
    }

    const int min_depth = 1;
    const int max_depth = 9999999;
    const bool include_unsafe = true;
    const CAmount minimum_amount = 0;
    const CAmount maximum_amount = MAX_MONEY;
    const CAmount minimum_sum_amount = MAX_MONEY;
    uint64_t maximum_count = 0;

    UniValue results(UniValue::VARR);
    std::vector<COutput> outputs;
    assert(pwallet != nullptr);
    LOCK2(cs_main, pwallet->cs_wallet);

    pwallet->AvailableCoins(
            outputs,
            !include_unsafe,
            nullptr,
            minimum_amount,
            maximum_amount,
            minimum_sum_amount,
            maximum_count,
            min_depth,
            max_depth,
            true);

    for (const COutput& out : outputs) {
        CTxDestination address;
        const CScript& scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
        bool fValidAddress = ExtractDestination(scriptPubKey, address);

        if (destinations.size() && (!fValidAddress || !destinations.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("id", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));

        if (fValidAddress) {
            entry.push_back(Pair("address", EncodeDestination(address)));

            if (pwallet->mapAddressBook.count(address)) {
                entry.push_back(
                        Pair("account", pwallet->mapAddressBook[address].name));
            }

            CScript redeemScript;
            if (scriptPubKey.IsPayToScriptHash()) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                pwallet->GetCScript(hash, redeemScript);
            } else if (scriptPubKey.IsParameterizedPayToScriptHash()) {
                const CParamScriptID& hash = boost::get<CParamScriptID>(address);
                pwallet->GetParamScript(hash, redeemScript);
            }

            if (!redeemScript.empty()) {
                entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }

        entry.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
        entry.push_back(Pair("amount", out.tx->tx->vout[out.i].nValue));
        entry.push_back(Pair("confirmations", out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable));
        entry.push_back(Pair("solvable", out.fSolvable));
        results.push_back(entry);
    }

    return results;
}

UniValue fundrawtransaction(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
                            "fundrawtransaction \"hexstring\" ( options )\n"
                            "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                            "This will not modify existing inputs, and will add at most one change output to the outputs.\n"
                            "No existing outputs will be modified unless \"subtractFeeFromOutputs\" is specified.\n"
                            "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                            "The inputs added will not be signed, use signrawtransaction for that.\n"
                            "Note that all existing inputs must have their previous output transaction be in the wallet.\n"
                            "Note that all inputs selected must be of standard form and P2SH scripts must be\n"
                            "in the wallet using importaddress or addmultisigaddress (to calculate fees).\n"
                            "You can see whether this is the case by checking the \"solvable\" field in the listunspent output.\n"
                            "Only pay-to-pubkey, multisig, and P2SH versions thereof are currently supported for watch-only\n"
                            "\nArguments:\n"
                            "1. \"hexstring\"           (string, required) The hex string of the raw transaction\n"
                            "2. options                 (object, optional)\n"
                            "   {\n"
                            "     \"changeAddress\"          (string, optional, default pool address) The merit address to receive the change\n"
                            "     \"changePosition\"         (numeric, optional, default random) The index of the change output\n"
                            "     \"includeWatching\"        (boolean, optional, default false) Also select inputs which are watch only\n"
                            "     \"lockUnspents\"           (boolean, optional, default false) Lock selected unspent outputs\n"
                            "     \"feeRate\"                (numeric, optional, default not set: makes wallet determine the fee) Set a specific fee rate in " + CURRENCY_UNIT + "/kB\n"
                            "     \"subtractFeeFromOutputs\" (array, optional) A json array of integers.\n"
                            "                              The fee will be equally deducted from the amount of each specified output.\n"
                            "                              The outputs are specified by their zero-based index, before any change output is added.\n"
                            "                              Those recipients will receive less merits than you enter in their corresponding amount field.\n"
                            "                              If no outputs are specified here, the sender pays the fee.\n"
                            "                                  [vout_index,...]\n"
                            "     \"replaceable\"            (boolean, optional) Marks this transaction as BIP125 replaceable.\n"
                            "                              Allows this transaction to be replaced by a transaction with higher fees\n"
                            "     \"conf_target\"            (numeric, optional) Confirmation target (in blocks)\n"
                            "     \"estimate_mode\"          (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
                            "         \"UNSET\"\n"
                            "         \"ECONOMICAL\"\n"
                            "         \"CONSERVATIVE\"\n"
                            "   }\n"
                            "                         for backward compatibility: passing in a true instead of an object will result in {\"includeWatching\":true}\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"hex\":       \"value\", (string)  The resulting raw transaction (hex-encoded string)\n"
                            "  \"fee\":       n,         (numeric) Fee in " + CURRENCY_UNIT + " the resulting transaction pays\n"
                            "  \"changepos\": n          (numeric) The position of the added change output, or -1\n"
                            "}\n"
                            "\nExamples:\n"
                            "\nCreate a transaction with no inputs\n"
                            + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                            "\nAdd sufficient unsigned inputs to meet the output value\n"
                            + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") +
                            "\nSign the transaction\n"
                            + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") +
                            "\nSend the transaction\n"
                            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
                            );

    ObserveSafeMode();
    RPCTypeCheck(request.params, {UniValue::VSTR});

    CCoinControl coinControl;
    int changePosition = -1;
    bool lockUnspents = false;
    UniValue subtractFeeFromOutputs;
    std::set<int> setSubtractFeeFromOutputs;

    if (!request.params[1].isNull()) {
      if (request.params[1].type() == UniValue::VBOOL) {
        // backward compatibility bool only fallback
        coinControl.fAllowWatchOnly = request.params[1].get_bool();
      }
      else {
        RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});

        UniValue options = request.params[1];

        RPCTypeCheckObj(options,
            {
                {"changeAddress", UniValueType(UniValue::VSTR)},
                {"changePosition", UniValueType(UniValue::VNUM)},
                {"includeWatching", UniValueType(UniValue::VBOOL)},
                {"lockUnspents", UniValueType(UniValue::VBOOL)},
                {"reserveChangeKey", UniValueType(UniValue::VBOOL)}, // DEPRECATED (and ignored), should be removed in 0.16 or so.
                {"feeRate", UniValueType()}, // will be checked below
                {"subtractFeeFromOutputs", UniValueType(UniValue::VARR)},
                {"replaceable", UniValueType(UniValue::VBOOL)},
                {"conf_target", UniValueType(UniValue::VNUM)},
                {"estimate_mode", UniValueType(UniValue::VSTR)},
            },
            true, true);

        if (options.exists("changeAddress")) {
            CTxDestination dest = DecodeDestination(options["changeAddress"].get_str());

            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "changeAddress must be a valid merit address");
            }

            coinControl.destChange = dest;
        }

        if (options.exists("changePosition"))
            changePosition = options["changePosition"].get_int();

        if (options.exists("includeWatching"))
            coinControl.fAllowWatchOnly = options["includeWatching"].get_bool();

        if (options.exists("lockUnspents"))
            lockUnspents = options["lockUnspents"].get_bool();

        if (options.exists("feeRate"))
        {
            coinControl.m_feerate = CFeeRate(AmountFromValue(options["feeRate"]));
            coinControl.fOverrideFeeRate = true;
        }

        if (options.exists("subtractFeeFromOutputs"))
            subtractFeeFromOutputs = options["subtractFeeFromOutputs"].get_array();

        if (options.exists("replaceable")) {
            coinControl.signalRbf = options["replaceable"].get_bool();
        }
        if (options.exists("conf_target")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both conf_target and feeRate");
            }
            coinControl.m_confirm_target = ParseConfirmTarget(options["conf_target"]);
        }
        if (options.exists("estimate_mode")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both estimate_mode and feeRate");
            }
            if (!FeeModeFromString(options["estimate_mode"].get_str(), coinControl.m_fee_mode)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
            }
        }
      }
    }

    // parse hex string from parameter
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    if (tx.vout.size() == 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "TX must have at least one output");

    if (changePosition != -1 && (changePosition < 0 || (unsigned int)changePosition > tx.vout.size()))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "changePosition out of bounds");

    for (unsigned int idx = 0; idx < subtractFeeFromOutputs.size(); idx++) {
        int pos = subtractFeeFromOutputs[idx].get_int();
        if (setSubtractFeeFromOutputs.count(pos))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicated position: %d", pos));
        if (pos < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, negative position: %d", pos));
        if (pos >= int(tx.vout.size()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, position too large: %d", pos));
        setSubtractFeeFromOutputs.insert(pos);
    }

    CAmount nFeeOut;
    std::string strFailReason;

    if (!pwallet->FundTransaction(tx, nFeeOut, changePosition, strFailReason, lockUnspents, setSubtractFeeFromOutputs, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(tx)));
    result.push_back(Pair("changepos", changePosition));
    result.push_back(Pair("fee", ValueFromAmount(nFeeOut)));

    return result;
}

UniValue bumpfee(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "bumpfee \"txid\" ( options ) \n"
            "\nBumps the fee of an opt-in-RBF transaction T, replacing it with a new transaction B.\n"
            "An opt-in RBF transaction with the given txid must be in the wallet.\n"
            "The command will pay the additional fee by decreasing (or perhaps removing) its change output.\n"
            "If the change output is not big enough to cover the increased fee, the command will currently fail\n"
            "instead of adding new inputs to compensate. (A future implementation could improve this.)\n"
            "The command will fail if the wallet or mempool contains a transaction that spends one of T's outputs.\n"
            "By default, the new fee will be calculated automatically using estimatefee.\n"
            "The user can specify a confirmation target for estimatefee.\n"
            "Alternatively, the user can specify totalFee, or use RPC settxfee to set a higher fee rate.\n"
            "At a minimum, the new fee rate must be high enough to pay an additional new relay fee (incrementalfee\n"
            "returned by getnetworkinfo) to enter the node's mempool.\n"
            "\nArguments:\n"
            "1. txid                  (string, required) The txid to be bumped\n"
            "2. options               (object, optional)\n"
            "   {\n"
            "     \"confTarget\"        (numeric, optional) Confirmation target (in blocks)\n"
            "     \"totalFee\"          (numeric, optional) Total fee (NOT feerate) to pay, in satoshis.\n"
            "                         In rare cases, the actual fee paid might be slightly higher than the specified\n"
            "                         totalFee if the tx change output has to be removed because it is too close to\n"
            "                         the dust threshold.\n"
            "     \"replaceable\"       (boolean, optional, default true) Whether the new transaction should still be\n"
            "                         marked bip-125 replaceable. If true, the sequence numbers in the transaction will\n"
            "                         be left unchanged from the original. If false, any input sequence numbers in the\n"
            "                         original transaction that were less than 0xfffffffe will be increased to 0xfffffffe\n"
            "                         so the new transaction will not be explicitly bip-125 replaceable (though it may\n"
            "                         still be replaceable in practice, for example if it has unconfirmed ancestors which\n"
            "                         are replaceable).\n"
            "     \"estimate_mode\"     (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
            "         \"UNSET\"\n"
            "         \"ECONOMICAL\"\n"
            "         \"CONSERVATIVE\"\n"
            "   }\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\":    \"value\",   (string)  The id of the new transaction\n"
            "  \"origfee\":  n,         (numeric) Fee of the replaced transaction\n"
            "  \"fee\":      n,         (numeric) Fee of the new transaction\n"
            "  \"errors\":  [ str... ] (json array of strings) Errors encountered during processing (may be empty)\n"
            "}\n"
            "\nExamples:\n"
            "\nBump the fee, get the new transaction\'s txid\n" +
            HelpExampleCli("bumpfee", "<txid>"));
    }

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});
    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    // optional parameters
    CAmount totalFee = 0;
    CCoinControl coin_control;
    coin_control.signalRbf = true;
    if (!request.params[1].isNull()) {
        UniValue options = request.params[1];
        RPCTypeCheckObj(options,
            {
                {"confTarget", UniValueType(UniValue::VNUM)},
                {"totalFee", UniValueType(UniValue::VNUM)},
                {"replaceable", UniValueType(UniValue::VBOOL)},
                {"estimate_mode", UniValueType(UniValue::VSTR)},
            },
            true, true);

        if (options.exists("confTarget") && options.exists("totalFee")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "confTarget and totalFee options should not both be set. Please provide either a confirmation target for fee estimation or an explicit total fee for the transaction.");
        } else if (options.exists("confTarget")) { // TODO: alias this to conf_target
            coin_control.m_confirm_target = ParseConfirmTarget(options["confTarget"]);
        } else if (options.exists("totalFee")) {
            totalFee = options["totalFee"].get_int64();
            if (totalFee <= 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid totalFee %s (must be greater than 0)", FormatMoney(totalFee)));
            }
        }

        if (options.exists("replaceable")) {
            coin_control.signalRbf = options["replaceable"].get_bool();
        }
        if (options.exists("estimate_mode")) {
            if (!FeeModeFromString(options["estimate_mode"].get_str(), coin_control.m_fee_mode)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
            }
        }
    }

    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    CFeeBumper feeBump(pwallet, hash, coin_control, totalFee);
    BumpFeeResult res = feeBump.getResult();
    if (res != BumpFeeResult::OK)
    {
        switch(res) {
            case BumpFeeResult::INVALID_ADDRESS_OR_KEY:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::INVALID_REQUEST:
                throw JSONRPCError(RPC_INVALID_REQUEST, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::INVALID_PARAMETER:
                throw JSONRPCError(RPC_INVALID_PARAMETER, feeBump.getErrors()[0]);
                break;
            case BumpFeeResult::WALLET_ERROR:
                throw JSONRPCError(RPC_WALLET_ERROR, feeBump.getErrors()[0]);
                break;
            default:
                throw JSONRPCError(RPC_MISC_ERROR, feeBump.getErrors()[0]);
                break;
        }
    }

    // sign bumped transaction
    if (!feeBump.signTransaction(pwallet)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Can't sign transaction.");
    }
    // commit the bumped transaction
    if(!feeBump.commit(pwallet)) {
        throw JSONRPCError(RPC_WALLET_ERROR, feeBump.getErrors()[0]);
    }
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("txid", feeBump.getBumpedTxId().GetHex()));
    result.push_back(Pair("origfee", ValueFromAmount(feeBump.getOldFee())));
    result.push_back(Pair("fee", ValueFromAmount(feeBump.getNewFee())));
    UniValue errors(UniValue::VARR);
    for (const std::string& err: feeBump.getErrors())
        errors.push_back(err);
    result.push_back(Pair("errors", errors));

    return result;
}

UniValue generate(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (pwallet->IsLocked()) {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    }

    // check that wallet is alredy referred or has unlock transaction
    if (!pwallet->IsReferred() && pwallet->mapWalletRTx.empty()) {
        throw JSONRPCError(RPC_WALLET_NOT_REFERRED, "Error: Wallet is not unlocked. Use referrer address to unlock first. See 'unlockwallet'");
    }

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3) {
        throw std::runtime_error(
            "generate nblocks ( maxtries )\n"
            "\nMine up to nblocks blocks immediately (before the RPC call returns) to an address in the wallet.\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, required) How many blocks are generated immediately.\n"
            "2. maxtries     (numeric, optional) How many iterations to try (default = 1000000).\n"
            "3. nthreads     (numeric, optional) Set the number of threads for mining. Can be -1 for unlimited.\n"

            "\nResult:\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks\n"
            + HelpExampleCli("generate", "11")
        );
    }

    int num_generate = request.params[0].get_int();
    uint64_t max_tries = 1000000;
    if (!request.params[1].isNull()) {
        max_tries = request.params[1].get_int();
    }

    int nThreads = DEFAULT_MINING_THREADS;

    if (!request.params[2].isNull()) {
        nThreads = request.params[2].get_int();
    }

    std::shared_ptr<CReserveScript> coinbase_script;
    pwallet->GetScriptForMining(coinbase_script);

    // If the keypool is exhausted, no script is returned at all.  Catch this.
    if (!coinbase_script) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    //throw an error if no script was provided
    if (coinbase_script->reserveScript.empty()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No coinbase script available");
    }

    return generateBlocks(coinbase_script, num_generate, max_tries, true, nThreads);
}

UniValue unlockwallet(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1 || request.params[0].get_str().empty() || request.params.size() > 2) {
        throw std::runtime_error(
            "unlockwallet \"parentaddress\"\n"
            "Updates the wallet with referral code and beacons first key with associated referral.\n"
            "Returns an object containing various wallet state info.\n"
            "\nArguments:\n"
            "1. parentaddress   (string, required) Parent address needed to unlock the wallet.\n"
            "2. tag             (stirng, optional) wallet unique id"
            "\nResult:\n"
            "{\n"
            "  \"address\": xxxxx,                (string) the wallet's root address. it's a referral address to use to share with other users\n"
            "  \"walletname\": xxxxx,             (string) the wallet db file name\n"
            "  \"walletversion\": xxxxx,          (numeric) the wallet version\n"
            "  \"tag\": xxxxx,                    (string, optional) the wallet tag\n"
            "  \"balance\": xxxxxxx,              (numeric) the total confirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"unconfirmed_balance\": xxx,      (numeric) the total unconfirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"immature_balance\": xxxxxx,      (numeric) the total immature balance of the wallet in " + CURRENCY_UNIT + "\n"
            "  \"txcount\": xxxxxxx,              (numeric) the total number of transactions in the wallet\n"
            "  \"keypoololdest\": xxxxxx,         (numeric) the timestamp (seconds since Unix epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,             (numeric) how many new keys are pre-generated (only counts external keys)\n"
            "  \"keypoolsize_hd_internal\": xxxx, (numeric) how many new keys are pre-generated for internal use (used for change outputs, only appears if the wallet is using this feature, otherwise external keys are used)\n"
            "  \"unlocked_until\": ttt,           (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,              (numeric) the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB\n"
            "  \"hdmasterkeyid\": \"<hash160>\"   (string) the Hash160 of the HD master pubkey\n"
            "  \"referred\": true|false           (boolean) if wallet is referred\n"
            "  \"referraladdress\": xxxxxx        (string) referral address to use to share with other users\n"
            "  \"invites\": xxxxxx                (numeric) number of available invites\n"
            "  \"immature_invites\": xxxxxx       (numeric) number of immature invites\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("unlockwallet", "\"parentaddress\"")
            + HelpExampleRpc("unlockwallet", "\"parentaddress\"")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    CMeritAddress parentAddress{request.params[0].get_str()};

    if (!parentAddress.IsValid()) {
        throw std::runtime_error(strprintf("Parent address \"%s\" is not valid or in wrong format.", parentAddress.ToString().c_str()));
    }

    auto parentAddressUint160 = parentAddress.GetUint160();
    assert(parentAddressUint160);

    auto tag = request.params.size() == 2 ? request.params[1].get_str() : "";

    if (tag.size() > referral::MAX_TAG_LENGTH) {
        throw std::runtime_error(strprintf("Tag length should not be more than %d.", referral::MAX_TAG_LENGTH));
    }

    referral::ReferralRef referral = pwallet->Unlock(*parentAddressUint160, tag);

    // TODO: Make this check more robust.
    UniValue obj(UniValue::VOBJ);

    obj.push_back(Pair("walletname", pwallet->GetName()));
    obj.push_back(Pair("walletversion", pwallet->GetVersion()));
    obj.push_back(Pair("tag", pwallet->GetTag()));
    obj.push_back(Pair("balance", ValueFromAmount(pwallet->GetBalance())));
    obj.push_back(Pair("unconfirmed_balance", ValueFromAmount(pwallet->GetUnconfirmedBalance())));
    obj.push_back(Pair("immature_balance", ValueFromAmount(pwallet->GetImmatureBalance())));
    obj.push_back(Pair("txcount", (int)pwallet->mapWallet.size()));
    obj.push_back(Pair("keypoololdest", pwallet->GetOldestKeyPoolTime()));
    obj.push_back(Pair("keypoolsize", (int64_t)pwallet->GetKeyPoolSize()));
    CKeyID masterKeyID = pwallet->GetHDChain().masterKeyID;

    if (pwallet->IsCrypted())
        obj.push_back(Pair("unlocked_until", pwallet->nRelockTime));

    obj.push_back(Pair("paytxfee", ValueFromAmount(payTxFee.GetFeePerK())));

    if (!masterKeyID.IsNull())
        obj.push_back(Pair("hdmasterkeyid", masterKeyID.GetHex()));

    obj.push_back(Pair("referred", true));
    obj.push_back(Pair("referraladdress", EncodeDestination(CKeyID{referral->GetAddress()})));
    obj.push_back(Pair("invites", pwallet->GetAvailableBalance(nullptr, true)));
    obj.push_back(Pair("immature_invites", pwallet->GetImmatureBalance(true)));

    return obj;
}

UniValue beaconaddress(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || (request.params.size() != 3 && request.params.size() != 4)) {
        throw std::runtime_error(
            "beaconaddress \"address\" \"signingkey\" \"parentaddress\"\n"
            "signs and beacons an address with the signing key specified\n"
            "\nArguments:\n"
            "1. address         (string, required) Parent address needed to unlock the wallet.\n"
            "2. signingkey      (string, required) key used to sign the referral in WIF format.\n"
            "3. parentaddress   (string, required) Parent address needed to unlock the wallet.\n"
            "4. tag             (string, optional) address unique id"
            "\nResult:\n"
            "{\n"
            "  \"beaconid\": xxxxx,               (string) id of the beacon\n"
            "  \"address\": xxxxx,                (string) address beaconed\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("beaconaddress", "\"address\" \"key\" \"parentaddress\"")
            + HelpExampleRpc("beaconaddress", "\"address\" \"key\" \"parentaddress\"")
        );
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);
    if(request.params.size() == 3) {

        CMeritAddress address(request.params[0].get_str());

        CMeritSecret signing_key_secret;
        signing_key_secret.SetString(request.params[1].get_str());

        CMeritAddress parent_address(request.params[2].get_str());

        if (!address.IsValid()) {
            std::stringstream e;
            e << "Address " << address.ToString() << " is not valid or in wrong format.";
            throw std::runtime_error(e.str());
        }

        if(signing_key_secret.GetSize() < 32) {
            std::stringstream e;
            e << "The signing key needs to be greater or equal to 32 bytes in size. Got " << signing_key_secret.GetSize() << " instead.";
            throw std::runtime_error(e.str());
        }

        auto key = signing_key_secret.GetKey();
        if (!key.IsValid()) {
            throw std::runtime_error("The signing key needs to be in the Wallet Import Format");
        }

        if (!parent_address.IsValid()) {
            throw std::runtime_error(strprintf("Parent address \"%s\" is not valid or in wrong format.", parent_address.ToString().c_str()));
        }

        auto referral = pwallet->GenerateNewReferral(
                address.GetType(),
                *address.GetUint160(),
                key.GetPubKey(),
                *parent_address.GetUint160(),
                request.params[3].get_str(),
                key);

        if(!referral) {
            throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "Unable to generate referral for receiver key");
        }

        // TODO: Make this check more robust.
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("beaconid", referral->GetHash().GetHex()));
        obj.push_back(Pair("address", CMeritAddress{referral->addressType, referral->GetAddress()}.ToString()));
    } else {

        CMeritAddress address(request.params[0].get_str());
        CPubKey pub_key{ParseHex(request.params[1].get_str())};
        CMeritAddress parent_address(request.params[2].get_str());

        auto parent_addr_uint160 = parent_address.GetUint160() ?
            * parent_address.GetUint160() : referral::Address{};

        referral::Referral ref{
            referral::MutableReferral(
                address.GetType(), *address.GetUint160(), pub_key, parent_addr_uint160)
        };

        auto hash = (CHashWriter(SER_GETHASH, 0) << parent_addr_uint160 << ref.GetAddress()).GetHash();

        obj.push_back(Pair("referral_data_to_sign", hash.GetHex()));
    }

    return obj;
}

UniValue getanv(const JSONRPCRequest& request)
{
    assert(prefviewdb);

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getanv\n"
            "\nReturns the wallet's ANV.\n"
            "\nResult:\n"
            "ANV              (numeric) The total Aggregate Network Value in " + CURRENCY_UNIT + " received for the keys or wallet.\n"
            "\nExamples:\n"
            + HelpExampleCli("getanv", "")
        );
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<referral::Address> keys;

    auto addrs = pwallet->mapAddressBook;
    for (const auto &addrEntry: addrs) {
        CTxDestination dest = addrEntry.first;

        if (IsMine(*pwallet, dest)) {
            uint160 key;
            if(GetUint160(dest, key)) {
                keys.push_back(key);
            }
        }
    }

    auto anvs = pog::GetANVs(keys, *prefviewdb);

    auto total =
        std::accumulate(std::begin(anvs), std::end(anvs), CAmount{0},
            [](CAmount total, const referral::AddressANV& v){
                return total + v.anv;
            });

    return total;
}

#ifdef ENABLE_WALLET
UniValue getrewards(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "getrewards\n"
            "Return wallet rewards for being a miner or ambassador.\n"
            "\nResult:\n"
            "{\n"
            "   \"mining\": x.xxxx,     (numeric) The total amount in " + CURRENCY_UNIT + " received for this account for mining.\n"
            "   \"ambassador\": x.xxxx, (numeric) The total amount in " + CURRENCY_UNIT + " received for this account for being ambassador.\n"
            "}\n"
            "\nExamples:\n" + HelpExampleCli("getbalance", "")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue ret(UniValue::VOBJ);

    pog::RewardsAmount rewards = pwallet->GetRewards();

    ret.push_back(Pair("mining", ValueFromAmount(rewards.mining)));
    ret.push_back(Pair("ambassador", ValueFromAmount(rewards.ambassador)));

    return ret;
}

#endif

extern UniValue abortrescan(const JSONRPCRequest& request); // in rpcdump.cpp
extern UniValue dumpprivkey(const JSONRPCRequest& request); // in rpcdump.cpp
extern UniValue importprivkey(const JSONRPCRequest& request);
extern UniValue importaddress(const JSONRPCRequest& request);
extern UniValue importpubkey(const JSONRPCRequest& request);
extern UniValue dumpwallet(const JSONRPCRequest& request);
extern UniValue importwallet(const JSONRPCRequest& request);
extern UniValue importprunedfunds(const JSONRPCRequest& request);
extern UniValue removeprunedfunds(const JSONRPCRequest& request);
extern UniValue importmulti(const JSONRPCRequest& request);

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           argNames
    //  --------------------- ------------------------    -----------------------  ----------
    { "rawtransactions",    "fundrawtransaction",       &fundrawtransaction,       {"hexstring","options"} },
    { "hidden",             "resendwallettransactions", &resendwallettransactions, {} },
    { "wallet",             "abandontransaction",       &abandontransaction,       {"txid"} },
    { "wallet",             "abortrescan",              &abortrescan,              {} },
    { "wallet",             "addmultisigaddress",       &addmultisigaddress,       {"nrequired","keys","account"} },
    { "wallet",             "addwitnessaddress",        &addwitnessaddress,        {"address"} },
    { "wallet",             "backupwallet",             &backupwallet,             {"destination"} },
    { "wallet",             "bumpfee",                  &bumpfee,                  {"txid", "options"} },
    { "wallet",             "dumpprivkey",              &dumpprivkey,              {"address"}  },
    { "wallet",             "dumpwallet",               &dumpwallet,               {"filename"} },
    { "wallet",             "encryptwallet",            &encryptwallet,            {"passphrase"} },
    { "wallet",             "getaccountaddress",        &getaccountaddress,        {"account"} },
    { "wallet",             "getaccount",               &getaccount,               {"address"} },
    { "wallet",             "getaddressesbyaccount",    &getaddressesbyaccount,    {"account"} },
    { "wallet",             "getbalance",               &getbalance,               {"account","minconf","include_watchonly"} },
    { "wallet",             "getnewaddress",            &getnewaddress,            {"account"} },
    { "wallet",             "getrawchangeaddress",      &getrawchangeaddress,      {} },
    { "wallet",             "getreceivedbyaccount",     &getreceivedbyaccount,     {"account","minconf"} },
    { "wallet",             "getreceivedbyaddress",     &getreceivedbyaddress,     {"address","minconf"} },
    { "wallet",             "gettransaction",           &gettransaction,           {"txid","include_watchonly"} },
    { "wallet",             "getunconfirmedbalance",    &getunconfirmedbalance,    {} },
    { "wallet",             "getwalletinfo",            &getwalletinfo,            {} },
    { "wallet",             "importmulti",              &importmulti,              {"requests","options"} },
    { "wallet",             "importprivkey",            &importprivkey,            {"privkey","label","rescan"} },
    { "wallet",             "importwallet",             &importwallet,             {"filename"} },
    { "wallet",             "importaddress",            &importaddress,            {"address","label","rescan","p2sh"} },
    { "wallet",             "importprunedfunds",        &importprunedfunds,        {"rawtransaction","txoutproof"} },
    { "wallet",             "importpubkey",             &importpubkey,             {"pubkey","label","rescan"} },
    { "wallet",             "keypoolrefill",            &keypoolrefill,            {"newsize"} },
    { "wallet",             "listaccounts",             &listaccounts,             {"minconf","include_watchonly"} },
    { "wallet",             "listaddressgroupings",     &listaddressgroupings,     {} },
    { "wallet",             "listlockunspent",          &listlockunspent,          {} },
    { "wallet",             "listreceivedbyaccount",    &listreceivedbyaccount,    {"minconf","include_empty","include_watchonly"} },
    { "wallet",             "listreceivedbyaddress",    &listreceivedbyaddress,    {"minconf","include_empty","include_watchonly"} },
    { "wallet",             "listsinceblock",           &listsinceblock,           {"blockhash","target_confirmations","include_watchonly","include_removed"} },
    { "wallet",             "listtransactions",         &listtransactions,         {"account","count","skip","include_watchonly"} },
    { "wallet",             "listunspent",              &listunspent,              {"minconf","maxconf","addresses","include_unsafe","query_options"} },
    { "wallet",             "listwallets",              &listwallets,              {} },
    { "wallet",             "lockunspent",              &lockunspent,              {"unlock","transactions"} },
    { "wallet",             "move",                     &movecmd,                  {"fromaccount","toaccount","amount","minconf","comment"} },
    { "wallet",             "sendfrom",                 &sendfrom,                 {"fromaccount","toaddress","amount","minconf","comment","comment_to"} },
    { "wallet",             "sendmany",                 &sendmany,                 {"fromaccount","amounts","minconf","comment","subtractfeefrom","replaceable","conf_target","estimate_mode"} },
    { "wallet",             "sendtoaddress",            &sendtoaddress,            {"address","amount","comment","comment_to","subtractfeefromamount","replaceable","conf_target","estimate_mode"} },
    { "wallet",             "easysend",                 &easysend,                 {"amount", "password"} },
    { "wallet",             "easyreceive",              &easyreceive,              {"secret", "senderpubkey", "password"} },
    { "wallet",             "createvault",              &createvault,              {"amount", "options"} },
    { "wallet",             "renewvault",               &renewvault,               {"vaultaddress", "masterkey", "options"} },
    { "wallet",             "spendvault",               &spendvault,               {"vaultaddress", "amount", "destination"} },
    { "wallet",             "getvaultinfo",             &getvaultinfo,             {"vaultaddress"} },
    { "wallet",             "setaccount",               &setaccount,               {"address","account"} },
    { "wallet",             "settxfee",                 &settxfee,                 {"amount"} },
    { "wallet",             "signmessage",              &signmessage,              {"address","message"} },
    { "wallet",             "walletlock",               &walletlock,               {} },
    { "wallet",             "walletpassphrasechange",   &walletpassphrasechange,   {"oldpassphrase","newpassphrase"} },
    { "wallet",             "walletpassphrase",         &walletpassphrase,         {"passphrase","timeout"} },
    { "wallet",             "removeprunedfunds",        &removeprunedfunds,        {"txid"} },

    { "generating",         "generate",                 &generate,                 {"nblocks","maxtries"} },

    // merit specific commands
    { "referral",           "unlockwallet",             &unlockwallet,             {"parentaddress", "tag"} },
    { "referral",           "beaconaddress",            &beaconaddress,            {"address", "key", "parentaddress"} },
    { "referral",           "getanv",                   &getanv,                   {} },
    { "wallet",             "confirmaddress",           &confirmaddress,           {"address"} },
    { "wallet",             "listinvites",              &listinvites,              {"addresses"} },

    { "wallet",             "getrewards",               &getrewards,               {} },
};

void RegisterWalletRPCCommands(CRPCTable &t)
{
    if (gArgs.GetBoolArg("-disablewallet", false))
        return;

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
