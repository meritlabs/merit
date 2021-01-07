// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Merit Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"
#include "init.h"
#include "scheduler.h"
#include "sync.h"
#include "util.h"

#include <list>
#include <atomic>

#include <boost/signals2/signal.hpp>

struct MainSignalsInstance {
    boost::signals2::signal<void (const CBlockIndex *, const CBlockIndex *, bool fInitialDownload)> UpdatedBlockTip;
    boost::signals2::signal<void (const CTransactionRef &)> TransactionAddedToMempool;
    boost::signals2::signal<void (const referral::ReferralRef &)> ReferralAddedToMempool;
    boost::signals2::signal<void (const std::shared_ptr<const CBlock> &, const CBlockIndex *pindex, const std::vector<CTransactionRef>&)> BlockConnected;
    boost::signals2::signal<void (const std::shared_ptr<const CBlock> &)> BlockDisconnected;
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    boost::signals2::signal<void (const uint256 &)> Inventory;
    boost::signals2::signal<void (int64_t nBestBlockTime, CConnman* connman)> Broadcast;
    boost::signals2::signal<void (const CBlock&, const CValidationState&)> BlockChecked;
    boost::signals2::signal<void (const CBlockIndex *, const std::shared_ptr<const CBlock>&)> NewPoWValidBlock;

    // merit signals
    boost::signals2::signal<void (const referral::ReferralRef &)> ReferralTransactionAddedToMempool;
    /** Notifies listeners that a key for mining is required (coinbase) */
    boost::signals2::signal<void (std::shared_ptr<CReserveScript>&)> ScriptForMining;
    /** Notifies listeners that a block has been successfully mined */
    boost::signals2::signal<void (const uint256 &)> BlockFound;

    // We are not allowed to assume the scheduler only runs in one thread,
    // but must ensure all callbacks happen in-order, so we end up creating
    // our own queue here :(
    SingleThreadedSchedulerClient m_schedulerClient;

    explicit MainSignalsInstance(CScheduler *pscheduler) : m_schedulerClient(pscheduler) {}
};

static CMainSignals g_signals;

void CMainSignals::RegisterBackgroundSignalScheduler(CScheduler& scheduler) {
    assert(!m_internals);
    m_internals.reset(new MainSignalsInstance(&scheduler));
}

void CMainSignals::UnregisterBackgroundSignalScheduler() {
    m_internals.reset(nullptr);
}

void CMainSignals::FlushBackgroundCallbacks() {
    m_internals->m_schedulerClient.EmptyQueue();
}

CMainSignals& GetMainSignals()
{
    return g_signals;
}

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.m_internals->UpdatedBlockTip.connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    g_signals.m_internals->TransactionAddedToMempool.connect(boost::bind(&CValidationInterface::TransactionAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->ReferralAddedToMempool.connect(boost::bind(&CValidationInterface::ReferralAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->BlockConnected.connect(boost::bind(&CValidationInterface::BlockConnected, pwalletIn, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    g_signals.m_internals->BlockDisconnected.connect(boost::bind(&CValidationInterface::BlockDisconnected, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->Inventory.connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->Broadcast.connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->BlockChecked.connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->NewPoWValidBlock.connect(boost::bind(&CValidationInterface::NewPoWValidBlock, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->ReferralTransactionAddedToMempool.connect(boost::bind(&CValidationInterface::ReferralTransactionAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->ScriptForMining.connect(boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->BlockFound.connect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, boost::placeholders::_1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.m_internals->BlockChecked.disconnect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->Broadcast.disconnect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->Inventory.disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->SetBestChain.disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->TransactionAddedToMempool.disconnect(boost::bind(&CValidationInterface::TransactionAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->ReferralAddedToMempool.disconnect(boost::bind(&CValidationInterface::ReferralAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->BlockConnected.disconnect(boost::bind(&CValidationInterface::BlockConnected, pwalletIn, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    g_signals.m_internals->BlockDisconnected.disconnect(boost::bind(&CValidationInterface::BlockDisconnected, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->UpdatedBlockTip.disconnect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));
    g_signals.m_internals->NewPoWValidBlock.disconnect(boost::bind(&CValidationInterface::NewPoWValidBlock, pwalletIn, boost::placeholders::_1, boost::placeholders::_2));
    g_signals.m_internals->ReferralTransactionAddedToMempool.disconnect(boost::bind(&CValidationInterface::ReferralTransactionAddedToMempool, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->ScriptForMining.disconnect(boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, boost::placeholders::_1));
    g_signals.m_internals->BlockFound.disconnect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, boost::placeholders::_1));
}

void UnregisterAllValidationInterfaces() {
    g_signals.m_internals->BlockChecked.disconnect_all_slots();
    g_signals.m_internals->Broadcast.disconnect_all_slots();
    g_signals.m_internals->Inventory.disconnect_all_slots();
    g_signals.m_internals->SetBestChain.disconnect_all_slots();
    g_signals.m_internals->TransactionAddedToMempool.disconnect_all_slots();
    g_signals.m_internals->ReferralAddedToMempool.disconnect_all_slots();
    g_signals.m_internals->BlockConnected.disconnect_all_slots();
    g_signals.m_internals->BlockDisconnected.disconnect_all_slots();
    g_signals.m_internals->UpdatedBlockTip.disconnect_all_slots();
    g_signals.m_internals->NewPoWValidBlock.disconnect_all_slots();
    g_signals.m_internals->ReferralTransactionAddedToMempool.disconnect_all_slots();
    g_signals.m_internals->ScriptForMining.disconnect_all_slots();
    g_signals.m_internals->BlockFound.disconnect_all_slots();
}

void CMainSignals::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) {
    m_internals->UpdatedBlockTip(pindexNew, pindexFork, fInitialDownload);
}

void CMainSignals::TransactionAddedToMempool(const CTransactionRef &ptx) {
    m_internals->TransactionAddedToMempool(ptx);
}

void CMainSignals::BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindex, const std::vector<CTransactionRef>& vtxConflicted) {
    m_internals->BlockConnected(pblock, pindex, vtxConflicted);
}

void CMainSignals::BlockDisconnected(const std::shared_ptr<const CBlock> &pblock) {
    m_internals->BlockDisconnected(pblock);
}

void CMainSignals::SetBestChain(const CBlockLocator &locator) {
    m_internals->SetBestChain(locator);
}

void CMainSignals::Inventory(const uint256 &hash) {
    m_internals->Inventory(hash);
}

void CMainSignals::Broadcast(int64_t nBestBlockTime, CConnman* connman) {
    m_internals->Broadcast(nBestBlockTime, connman);
}

void CMainSignals::BlockChecked(const CBlock& block, const CValidationState& state) {
    m_internals->BlockChecked(block, state);
}

void CMainSignals::NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &block) {
    m_internals->NewPoWValidBlock(pindex, block);
}

void CMainSignals::ReferralAddedToMempool(const referral::ReferralRef &rtx) {
    m_internals->ReferralTransactionAddedToMempool(rtx);
}

void CMainSignals::ScriptForMining(std::shared_ptr<CReserveScript>& script) {
    m_internals->ScriptForMining(script);
}

void CMainSignals::BlockFound(const uint256& hash) {
    m_internals->BlockFound(hash);
}
