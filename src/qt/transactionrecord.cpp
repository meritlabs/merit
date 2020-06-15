// Copyright (c) 2017-2020 The Merit Foundation
// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

std::string FindFrom(CTransactionRef tx, const CWallet* wallet) {
    assert(tx);
    assert(wallet);

    LOCK(cs_main);
    std::string from;
    for(const auto& input : tx->vin) {
        CTransactionRef tx;
        uint256 hashBlock;
        if (!GetTransaction(
                    input.prevout.hash,
                    tx,
                    Params().GetConsensus(),
                    hashBlock,
                    false)) {
            continue;
        }
        const auto& out = tx->vout[input.prevout.n];
        CTxDestination address;
        if (ExtractDestination(out.scriptPubKey, address) && !IsMine(*wallet, address))
        {
            if(!from.empty()) {
                from += ", ";
            }

            uint160 address_bytes;
            if(GetUint160(address, address_bytes)) {
                auto alias = FindAliasForAddress(address_bytes);
                from += alias.empty() ? 
                    EncodeDestination(address) : 
                    "@" + FindAliasForAddress(address_bytes);
            }
        }
    }
    return from;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;
    const bool is_invite = wtx.tx->IsInvite();

    if (nNet > 0 || wtx.IsCoinBase())
    {
        auto from = FindFrom(wtx.tx, wallet);

        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                std::string encoded_address;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Merit Address
                    if(is_invite) {
                        sub.type = TransactionRecord::RecvInvite;
                    } else {
                        sub.type = from.empty() ?
                            TransactionRecord::RecvWithAddress:
                            TransactionRecord::RecvFromAddress;
                    }
                    encoded_address = EncodeDestination(address);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    if(is_invite) {
                        sub.type = TransactionRecord::RecvInvite;
                    } else {
                        sub.type = from.empty() ?
                            TransactionRecord::RecvFromOther:
                            TransactionRecord::RecvFromAddress;
                    }
                    sub.type = is_invite ?
                        TransactionRecord::RecvInvite:
                        TransactionRecord::RecvFromOther;
                    encoded_address = mapValue["from"];
                }

                uint160 address_bytes;
                std::string alias;
                if(GetUint160(address, address_bytes)) {
                    alias = FindAliasForAddress(address_bytes);
                }

                sub.to = alias.empty() ? encoded_address : ("@" + alias);
                sub.from = from;

                if (wtx.IsCoinBase())
                {
                    // Generated
                    if(is_invite)  {
                        sub.type = TransactionRecord::GeneratedInvite;
                    } else {
                        sub.type = i == 0 ?
                            TransactionRecord::Generated :
                            TransactionRecord::AmbassadorReward;
                    }
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const CTxOut& txout : wtx.tx->vout)
        {
            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            CAmount nChange = wtx.GetChange();

            parts.append(
                    TransactionRecord(
                        hash,
                        nTime,
                        TransactionRecord::SendToSelf,
                        "",
                        "",
                        -(nDebit - nChange),
                        nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            std::string from; 
            auto wallet_address = wallet->GetRootAddress();
            if(wallet_address) {
                std::string alias = wallet->GetAlias();
                from = alias.empty() ? 
                    EncodeDestination(CKeyID{*wallet_address})
                    : ("@" + alias);
            }

            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                std::string encoded_address;
                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Merit Address
                    sub.type = is_invite ? TransactionRecord::SendInvite : TransactionRecord::SendToAddress;
                    encoded_address = EncodeDestination(address);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = is_invite ? TransactionRecord::SendInvite : TransactionRecord::SendToOther;
                    encoded_address = mapValue["to"];
                }

                std::string alias;
                uint160 address_bytes;
                if(GetUint160(address, address_bytes)) {
                    alias = FindAliasForAddress(address_bytes);
                }

                sub.from = from;
                sub.to = alias.empty() ? encoded_address : "@" + alias;

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    if (!CheckFinalTx(wtx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.tx->nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.tx->nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded() const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}

bool TransactionRecord::IsInvite() const
{
    return type == Type::GeneratedInvite || type == Type::SendInvite || type == Type::RecvInvite;
}
