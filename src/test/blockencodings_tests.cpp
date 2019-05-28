// Copyright (c) 2017-2019 The Merit Foundation developers
// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "consensus/merkle.h"
#include "chainparams.h"
#include "random.h"

#include "test/test_merit.h"

#include <boost/test/unit_test.hpp>

std::vector<std::pair<uint256, CTransactionRef>> extra_txn;
std::vector<std::pair<uint256, referral::ReferralRef>> extra_refs;

struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(blockencodings_tests, RegtestingSetup)

static CBlock BuildBlockTestCase() {
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = 42;

    block.vtx.resize(3);
    block.vtx[0] = MakeTransactionRef(tx);
    block.nVersion = 42;
    block.hashPrevBlock = InsecureRand256();
    block.nBits = 0x207fffff;

    tx.vin[0].prevout.hash = InsecureRand256();
    tx.vin[0].prevout.n = 0;
    block.vtx[1] = MakeTransactionRef(tx);

    tx.vin.resize(10);
    for (size_t i = 0; i < tx.vin.size(); i++) {
        tx.vin[i].prevout.hash = InsecureRand256();
        tx.vin[i].prevout.n = 0;
    }
    block.vtx[2] = MakeTransactionRef(tx);

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
    assert(!mutated);
    while (!CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
    return block;
}

// Number of shared use_counts we expect for a tx we haven't touched
// == 2 (mempool + our copy from the GetSharedEntryValue call)
#define SHARED_TX_OFFSET 2

BOOST_AUTO_TEST_CASE(SimpleRoundTripTest)
{
    CTxMemPool pool;
    referral::ReferralTxMemPool refpool;
    TestMemPoolEntryHelper entry;
    CBlock block(BuildBlockTestCase());

    pool.addUnchecked(block.vtx[2]->GetHash(), entry.FromTx(*block.vtx[2]));
    BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 0);

    // Do a simple ShortTxIDs RT
    {
        BlockHeaderAndShortIDs shortIDs(block, true);

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << shortIDs;

        BlockHeaderAndShortIDs shortIDs2;
        stream >> shortIDs2;

        PartiallyDownloadedBlock partialBlock(&pool, &refpool);
        BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn, extra_refs) == READ_STATUS_OK);
        BOOST_CHECK( partialBlock.IsTxAvailable(0));
        BOOST_CHECK(!partialBlock.IsTxAvailable(1));
        BOOST_CHECK( partialBlock.IsTxAvailable(2));

        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 1);

        size_t poolSize = pool.size();
        pool.removeRecursive(*block.vtx[2]);
        BOOST_CHECK_EQUAL(pool.size(), poolSize - 1);

        CBlock block2;
        {
            PartiallyDownloadedBlock tmp = partialBlock;
            BOOST_CHECK(partialBlock.FillBlock(block2, {}, {}, {}) == READ_STATUS_INVALID); // No transactions
            partialBlock = tmp;
        }

        // Wrong transaction
        {
            PartiallyDownloadedBlock tmp = partialBlock;
            partialBlock.FillBlock(block2, {block.vtx[2]}, {}, {}); // Current implementation doesn't check txn here, but don't require that
            partialBlock = tmp;
        }
        bool mutated;
        BOOST_CHECK(block.hashMerkleRoot != BlockMerkleRoot(block2, &mutated));

        CBlock block3;
        BOOST_CHECK(partialBlock.FillBlock(block3, {block.vtx[1]}, {}, {}) == READ_STATUS_OK);
        BOOST_CHECK_EQUAL(block.GetHash().ToString(), block3.GetHash().ToString());
        BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block3, &mutated).ToString());
        BOOST_CHECK(!mutated);
    }
}

class TestHeaderAndShortIDs {
    // Utility to encode custom BlockHeaderAndShortIDs
public:
    CBlockHeader header;
    uint64_t nonce;
    std::vector<uint64_t> m_short_tx_ids;
    std::vector<uint64_t> m_short_ref_ids;
    std::vector<PrefilledTransaction> m_prefilled_txn;

    explicit TestHeaderAndShortIDs(const BlockHeaderAndShortIDs& orig) {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << orig;
        stream >> *this;
    }
    explicit TestHeaderAndShortIDs(const CBlock& block) :
        TestHeaderAndShortIDs(BlockHeaderAndShortIDs(block, true)) {}

    uint64_t GetShortID(const uint256& txhash) const {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << *this;
        BlockHeaderAndShortIDs base;
        stream >> base;
        return base.GetShortID(txhash);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(nonce);

        size_t m_short_tx_ids_size = m_short_tx_ids.size();
        size_t m_short_ref_ids_size = m_short_ref_ids.size();

        READWRITE(VARINT(m_short_tx_ids_size));
        READWRITE(VARINT(m_short_ref_ids_size));

        m_short_tx_ids.resize(m_short_tx_ids_size);
        m_short_ref_ids.resize(m_short_tx_ids_size);

        for (size_t i = 0; i < m_short_tx_ids.size(); i++) {
            uint32_t lsb = m_short_tx_ids[i] & 0xffffffff;
            uint16_t msb = (m_short_tx_ids[i] >> 32) & 0xffff;
            READWRITE(lsb);
            READWRITE(msb);
            m_short_tx_ids[i] = (uint64_t(msb) << 32) | uint64_t(lsb);
        }

        for (size_t i = 0; i < m_short_ref_ids.size(); i++) {
            uint32_t lsb = m_short_tx_ids[i] & 0xffffffff;
            uint16_t msb = (m_short_tx_ids[i] >> 32) & 0xffff;
            READWRITE(lsb);
            READWRITE(msb);
            m_short_ref_ids[i] = (uint64_t(msb) << 32) | uint64_t(lsb);
        }

        READWRITE(m_prefilled_txn);
    }
};

BOOST_AUTO_TEST_CASE(NonCoinbasePreforwardRTTest)
{
    CTxMemPool pool;
    referral::ReferralTxMemPool refpool;
    TestMemPoolEntryHelper entry;
    CBlock block(BuildBlockTestCase());

    pool.addUnchecked(block.vtx[2]->GetHash(), entry.FromTx(*block.vtx[2]));
    BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 0);

    uint256 txhash;

    // Test with pre-forwarding tx 1, but not coinbase
    {
        TestHeaderAndShortIDs shortIDs(block);
        shortIDs.m_prefilled_txn.resize(1);
        shortIDs.m_prefilled_txn[0] = {1, block.vtx[1]};
        shortIDs.m_short_tx_ids.resize(2);
        shortIDs.m_short_tx_ids[0] = shortIDs.GetShortID(block.vtx[0]->GetHash());
        shortIDs.m_short_tx_ids[1] = shortIDs.GetShortID(block.vtx[2]->GetHash());

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << shortIDs;

        BlockHeaderAndShortIDs shortIDs2;
        stream >> shortIDs2;

        PartiallyDownloadedBlock partialBlock(&pool, &refpool);
        BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn, extra_refs) == READ_STATUS_OK);
        BOOST_CHECK(!partialBlock.IsTxAvailable(0));
        BOOST_CHECK( partialBlock.IsTxAvailable(1));
        BOOST_CHECK( partialBlock.IsTxAvailable(2));

        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 1);

        CBlock block2;
        {
            PartiallyDownloadedBlock tmp = partialBlock;
            BOOST_CHECK(partialBlock.FillBlock(block2, {}, {}, {}) == READ_STATUS_INVALID); // No transactions
            partialBlock = tmp;
        }

        // Wrong transaction
        {
            PartiallyDownloadedBlock tmp = partialBlock;
            partialBlock.FillBlock(block2, {block.vtx[1]}, {}, {}); // Current implementation doesn't check txn here, but don't require that
            partialBlock = tmp;
        }
        bool mutated;
        BOOST_CHECK(block.hashMerkleRoot != BlockMerkleRoot(block2, &mutated));

        CBlock block3;
        PartiallyDownloadedBlock partialBlockCopy = partialBlock;
        BOOST_CHECK(partialBlock.FillBlock(block3, {block.vtx[0]}, {}, {}) == READ_STATUS_OK);
        BOOST_CHECK_EQUAL(block.GetHash().ToString(), block3.GetHash().ToString());
        BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block3, &mutated).ToString());
        BOOST_CHECK(!mutated);

        txhash = block.vtx[2]->GetHash();
        block.vtx.clear();
        block2.vtx.clear();
        block3.vtx.clear();
        BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 1); // + 1 because of partialBlockCopy.
    }
    BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 0);
}

BOOST_AUTO_TEST_CASE(SufficientPreforwardRTTest)
{
    CTxMemPool pool;
    referral::ReferralTxMemPool refpool;
    TestMemPoolEntryHelper entry;
    CBlock block(BuildBlockTestCase());

    pool.addUnchecked(block.vtx[1]->GetHash(), entry.FromTx(*block.vtx[1]));
    BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[1]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 0);

    uint256 txhash;

    // Test with pre-forwarding coinbase + tx 2 with tx 1 in mempool
    {
        TestHeaderAndShortIDs shortIDs(block);
        shortIDs.m_prefilled_txn.resize(2);
        shortIDs.m_prefilled_txn[0] = {0, block.vtx[0]};
        shortIDs.m_prefilled_txn[1] = {1, block.vtx[2]}; // id == 1 as it is 1 after index 1
        shortIDs.m_short_tx_ids.resize(1);
        shortIDs.m_short_tx_ids[0] = shortIDs.GetShortID(block.vtx[1]->GetHash());

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << shortIDs;

        BlockHeaderAndShortIDs shortIDs2;
        stream >> shortIDs2;

        PartiallyDownloadedBlock partialBlock(&pool, &refpool);
        BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn, extra_refs) == READ_STATUS_OK);
        BOOST_CHECK( partialBlock.IsTxAvailable(0));
        BOOST_CHECK( partialBlock.IsTxAvailable(1));
        BOOST_CHECK( partialBlock.IsTxAvailable(2));

        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[1]->GetHash())->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 1);

        CBlock block2;
        PartiallyDownloadedBlock partialBlockCopy = partialBlock;
        BOOST_CHECK(partialBlock.FillBlock(block2, {}, {}, {}) == READ_STATUS_OK);
        BOOST_CHECK_EQUAL(block.GetHash().ToString(), block2.GetHash().ToString());
        bool mutated;
        BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block2, &mutated).ToString());
        BOOST_CHECK(!mutated);

        txhash = block.vtx[1]->GetHash();
        block.vtx.clear();
        block2.vtx.clear();
        BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 1); // + 1 because of partialBlockCopy.
    }
    BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedEntryValue().use_count(), SHARED_TX_OFFSET + 0);
}

BOOST_AUTO_TEST_CASE(EmptyBlockRoundTripTest)
{
    CTxMemPool pool;
    referral::ReferralTxMemPool refpool;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig.resize(10);
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 42;

    CBlock block;
    block.vtx.resize(1);
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));
    block.nVersion = 42;
    block.hashPrevBlock = InsecureRand256();
    block.nBits = 0x207fffff;

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
    assert(!mutated);
    while (!CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;

    // Test simple header round-trip with only coinbase
    {
        BlockHeaderAndShortIDs shortIDs(block, false);

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << shortIDs;

        BlockHeaderAndShortIDs shortIDs2;
        stream >> shortIDs2;

        PartiallyDownloadedBlock partialBlock(&pool, &refpool);
        BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn, extra_refs) == READ_STATUS_OK);
        BOOST_CHECK(partialBlock.IsTxAvailable(0));

        CBlock block2;
        std::vector<CTransactionRef> vtx_missing;
        std::vector<CTransactionRef> inv_missing;
        std::vector<referral::ReferralRef> vrefs_missing;
        BOOST_CHECK(partialBlock.FillBlock(block2, vtx_missing, inv_missing, vrefs_missing) == READ_STATUS_OK);
        BOOST_CHECK_EQUAL(block.GetHash().ToString(), block2.GetHash().ToString());
        BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block2, &mutated).ToString());
        BOOST_CHECK(!mutated);
    }
}

BOOST_AUTO_TEST_CASE(TransactionsRequestSerializationTest) {
    BlockTransactionsRequest req1;
    req1.blockhash = InsecureRand256();
    req1.m_transaction_indices.resize(4);
    req1.m_transaction_indices[0] = 0;
    req1.m_transaction_indices[1] = 1;
    req1.m_transaction_indices[2] = 3;
    req1.m_transaction_indices[3] = 4;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << req1;

    BlockTransactionsRequest req2;
    stream >> req2;

    BOOST_CHECK_EQUAL(req1.blockhash.ToString(), req2.blockhash.ToString());
    BOOST_CHECK_EQUAL(req1.m_transaction_indices.size(), req2.m_transaction_indices.size());
    BOOST_CHECK_EQUAL(req1.m_transaction_indices[0], req2.m_transaction_indices[0]);
    BOOST_CHECK_EQUAL(req1.m_transaction_indices[1], req2.m_transaction_indices[1]);
    BOOST_CHECK_EQUAL(req1.m_transaction_indices[2], req2.m_transaction_indices[2]);
    BOOST_CHECK_EQUAL(req1.m_transaction_indices[3], req2.m_transaction_indices[3]);
}

BOOST_AUTO_TEST_SUITE_END()
