#include "wallet/vault.h"

#include "script/standard.h"
#include "rpc/protocol.h"
#include "txmempool.h"
#include "validation.h"
#include "sync.h"
#include <algorithm>
#include "core_io.h"


namespace vault
{

template <class Transactions>
void ConvertToVaultOutputs(const Transactions& txns, VaultOutputs& outputs)
{
    using Pair = typename Transactions::value_type;
    outputs.resize(outputs.size() + txns.size());

    std::transform(std::begin(txns), std::end(txns), std::begin(outputs),
        [](const Pair& p) {
            return COutPoint{
                p.first.txhash,
                static_cast<uint32_t>(p.first.index)};
        });
}

VaultOutputs GetUnspentOutputs(CCoinsViewCache& view, const VaultOutputs& outputs)
{
    VaultOutputs unspent;
    unspent.reserve(outputs.size());
    std::copy_if(std::begin(outputs), std::end(outputs), std::back_inserter(unspent),
            [&view](const COutPoint& p) {
                return view.HaveCoin(p);
            });

    return unspent;
}

VaultCoins GetUnspentCoins(CCoinsViewCache& view, const VaultOutputs& unspent)
{
    VaultCoins coins(unspent.size());
    std::transform(std::begin(unspent), std::end(unspent), std::begin(coins),
            [&view](const COutPoint& p) {
                const auto& c = view.AccessCoin(p);
                return std::make_pair(p, c);
            });
    return coins;
}

VaultCoins FilterVaultCoins(const VaultCoins& coins, const uint160& address)
{
    VaultCoins vault_coins;
    vault_coins.reserve(coins.size());
    std::copy_if(coins.begin(), coins.end(), std::back_inserter(vault_coins),
            [&address](const VaultCoin& coin) {
                CTxDestination dest;
                if(!ExtractDestination(coin.second.out.scriptPubKey, dest)) {
                    return false;
                }
                auto script_id = boost::get<CParamScriptID>(&dest);
                if(!script_id) {
                    return false;
                }
                return *script_id == address;
            });
    return vault_coins;
}

VaultCoins FindUnspentVaultCoins(const uint160& address)
{
    VaultOutputs outputs;

    //Get outputs from mempool
    const int PARAM_SCRIPT_TYPE = 3;
    std::vector<std::pair<uint160, int> > addresses = {{address, PARAM_SCRIPT_TYPE}};
    using MempoolOutputs = std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>;
    MempoolOutputs mempool_outputs;
    mempool.getAddressIndex(addresses, mempool_outputs);
    ConvertToVaultOutputs(mempool_outputs, outputs);

    //Get outputs from chain
    using ChainOutputs = std::vector<std::pair<CAddressIndexKey, CAmount>>;
    ChainOutputs chain_outputs;
    GetAddressIndex(address, PARAM_SCRIPT_TYPE, chain_outputs);
    ConvertToVaultOutputs(chain_outputs, outputs);

    //Filter outputs and return only unspent coins
    LOCK(mempool.cs);
    CCoinsViewCache &view_chain = *pcoinsTip;
    CCoinsViewMemPool viewMempool(&view_chain, mempool);
    CCoinsViewCache view(&viewMempool);

    auto unspent_outputs = GetUnspentOutputs(view, outputs);
    auto unspent_coins = GetUnspentCoins(view, unspent_outputs);
    return FilterVaultCoins(unspent_coins, address);
}

Vault ParseVaultCoin(const VaultCoin& coin)
{
    Vault vault;

    vault.txid = coin.first.hash;
    vault.coin = coin.second;
    vault.out_point = coin.first;

    const auto& output = coin.second.out;
    const auto& scriptPubKey = output.scriptPubKey;

    CScript script_params;
    if(!scriptPubKey.ExtractParameterizedPayToScriptHashParams(script_params)) {
        throw JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                "The address is not a vault");
    }

    Stack stack;
    ScriptError serror;
    EvalPushOnlyScript(stack, script_params, SCRIPT_VERIFY_MINIMALDATA, &serror);

    if(stack.empty()) {
        throw JSONRPCError(
                RPC_MISC_ERROR,
                "Unexpectedly couldn't parse vault params");
    }


    const CScriptNum type_num(stack.back(), true);
    vault.type = type_num.getint();

    if(vault.type == 0 /* simple */) {
        const int stack_size = stack.size();

        if(stack_size < 5) {
            std::stringstream e;
            e << "Simple vault requires 5 or more parameters. " 
              << stack_size 
              << " were provided"; 

            throw JSONRPCError(
                    RPC_TYPE_ERROR,
                    e.str());
        }

        const auto& vault_tag = stack[stack_size - 2];
        vault.tag = uint160{vault_tag};

        const auto num_address_idx = stack_size - 3;
        CScriptNum script_num_addresses(stack[num_address_idx], false);
        auto num_addresses = script_num_addresses.getint();

        if(stack_size < 3 + num_addresses) {
            throw JSONRPCError(
                    RPC_MISC_ERROR,
                    "Vault seems to be incompatible");
        }

        for(int i = num_address_idx - num_addresses; i < num_address_idx; i++) {
            vault.whitelist.push_back(stack[i]);
        }

        auto vault_script = 
            GetScriptForSimpleVault(uint160{vault_tag}, num_addresses);

        vault.script = vault_script;
        vault.address = vault_script;
        vault.spend_pub_key.Set(stack[0]);
        vault.master_pub_key.Set(stack[1]);
    }

    return vault;
}

Vaults ParseVaultCoins(const VaultCoins& coins)
{
    Vaults vaults(coins.size());
    std::transform(coins.begin(), coins.end(), vaults.begin(), ParseVaultCoin);
    return vaults;
}

} //namespace vault
