// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_WALLET_VAULT_H
#define MERIT_WALLET_VAULT_H

#include "coins.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"

#include <utility>
#include <vector>

namespace vault
{

using VaultCoin = std::pair<COutPoint, Coin>;
using VaultCoins = std::vector<VaultCoin>;
using VaultOutputs = std::vector<COutPoint>;

VaultOutputs GetUnspentOutputs(CCoinsViewCache& view, const VaultOutputs& outputs);
VaultCoins GetUnspentCoins(CCoinsViewCache& view, const VaultOutputs& unspent);

VaultCoins FilterVaultCoins(const VaultCoins& coins, const uint160& address);
VaultCoins FindUnspentVaultCoins(const uint160& address);

struct Vault
{
    int type;
    uint256 txid;
    uint160 tag;
    COutPoint out_point;
    Coin coin;
    CScript script;
    CPubKey spend_pub_key;
    CPubKey renew_pub_key;
};

using Vaults = std::vector<Vault>;

Vault ParseVaultCoin(const VaultCoin& coin);
Vaults ParseVaultCoins(const VaultCoins& coins);

} //namespace vault

#endif // MERIT_WALLET_VAULT_H

