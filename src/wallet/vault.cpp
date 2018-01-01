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

bool Vault::SameKind(const Vault& o) const 
{
    return 
    type == o.type &&
    coin.out.scriptPubKey == o.coin.out.scriptPubKey;
}

using MempoolOutput = std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>;
using MempoolOutputs = std::vector<MempoolOutput>;

void FilterMempoolOutputs(const MempoolOutputs& outputs, MempoolOutputs& filtered)
{
    std::set<uint256> spending;

    for(const auto& out : outputs) { 
        if(!out.first.spending) continue;

        spending.insert(out.second.prevhash);
    }

    filtered.reserve(outputs.size());

    std::copy_if(outputs.begin(), outputs.end(), std::back_inserter(filtered),
            [&spending](const MempoolOutput& out) { 
                return !spending.count(out.first.txhash);
            });
}

template <class Transactions>
void ConvertToVaultOutputs(const Transactions& txns, VaultOutputs& outputs)
{
    using Transaction = typename Transactions::value_type;
    Transactions utxos;
    utxos.reserve(txns.size());

    std::copy_if(txns.begin(), txns.end(), std::back_inserter(utxos), 
            [](const Transaction& t) { 
                return !t.first.spending;
            });

    outputs.resize(outputs.size() + utxos.size());

    std::transform(utxos.begin(), utxos.end(), outputs.begin(),
        [](const Transaction& p) {
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
    MempoolOutputs mempool_outputs;
    mempool.getAddressIndex(addresses, mempool_outputs);

    MempoolOutputs filtered_mempool_outputs;
    FilterMempoolOutputs(mempool_outputs, filtered_mempool_outputs);
    ConvertToVaultOutputs(filtered_mempool_outputs, outputs);

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


void ExtractPubKeys(const Stack& stack, int num_keys_idx, PubKeys& keys)
{
        if(num_keys_idx < 1) { 
            throw JSONRPCError(RPC_TYPE_ERROR, 
                    "Vault seem sto be incompatible");
        }
        
        const auto num_keys = CScriptNum{stack[num_keys_idx], false}.getint();
        if(num_keys > num_keys_idx) {
            throw JSONRPCError(RPC_TYPE_ERROR, 
                    "Vault does not have expected amount of keys");
        } 

        for(int i = num_keys_idx - num_keys; i < num_keys_idx; i++) {
            CPubKey key{stack[i]};
            keys.push_back(key);
        }
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
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "The address is not a vault");
    }

    Stack stack;
    ScriptError serror;
    EvalPushOnlyScript(stack, script_params, SCRIPT_VERIFY_MINIMALDATA, &serror);

    if(stack.empty()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unexpectedly couldn't parse vault params");
    }


    const CScriptNum type_num(stack.back(), true);
    vault.type = type_num.getint();

    const int stack_size = stack.size();

    if(stack_size < 3) {
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot extract whitelist from vault");
    }

    const auto& vault_tag = stack[stack_size - 2];
    vault.tag = uint160{vault_tag};

    const auto num_address_idx = stack_size - 3;
    const auto num_addresses = CScriptNum{stack[num_address_idx], false}.getint();

    if(stack_size < 3 + num_addresses) {
        throw JSONRPCError(RPC_MISC_ERROR, "Vault seems to be incompatible");
    }

    for(int i = num_address_idx - num_addresses; i < num_address_idx; i++) {
        vault.whitelist.push_back(stack[i]);
    }

    auto spendlimit_idx = num_address_idx - num_addresses - 1;
    vault.spendlimit = CScriptNum{stack[spendlimit_idx], true, 8}.getint64();

    if(vault.type == 0) {

        if(stack_size < 6) {
            std::stringstream e;
            e << "Simple vault requires 6 or more parameters. " 
              << stack_size 
              << " were provided"; 

            throw JSONRPCError(RPC_TYPE_ERROR, e.str());
        }

        const auto vault_script = GetScriptForSimpleVault(uint160{vault_tag});

        vault.script = vault_script;
        vault.spend_pub_key.Set(stack[0]);
        vault.master_pub_key.Set(stack[1]);

    } else if (vault.type == 1) {


        const auto num_master_keys_idx = spendlimit_idx - 1;
        ExtractPubKeys(stack, num_master_keys_idx, vault.master_keys);

        const auto num_spend_keys_idx = num_master_keys_idx - vault.master_keys.size() - 1;
        ExtractPubKeys(stack, num_spend_keys_idx, vault.spend_keys);

        const auto vault_script = GetScriptForMultisigVault(uint160{vault_tag});

        vault.script = vault_script;

    } else {
        std::stringstream e;
        e << "Vault of type " << vault.type << " is not supported";
        throw JSONRPCError(RPC_MISC_ERROR, e.str());
    }

    vault.address = vault.script;
    return vault;
}

Vaults ParseVaultCoins(const VaultCoins& coins)
{
    Vaults vaults(coins.size());
    std::transform(coins.begin(), coins.end(), vaults.begin(), ParseVaultCoin);
    return vaults;
}

} //namespace vault
