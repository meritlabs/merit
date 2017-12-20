// Copyright (c) 2013-2017 The Merit Foundation developers
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include "chainparamsseeds.h"
#include "cuckoo/miner.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <numeric>
#include <set>
#include <time.h>
#include <vector>

using addressPrefix = std::vector<unsigned char>;

static CBlock CreateGenesisBlock(
    const char* pszTimestamp,
    const CScript& genesisOutputScript,
    uint32_t nTime,
    uint32_t nNonce,
    uint32_t nBits,
    uint8_t nEdgeBits,
    int32_t nVersion,
    const CAmount& genesisReward,
    Consensus::Params& params,
    addressPrefix pkPrefix,
    bool findPoW)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    // compressed pubkey
    auto rawKeyStr = ParseHex("0337d249c44b0327389a65687c7e9a823271a8c4355c74378d0b608b3339480e9a");
    CPubKey rawPubKey{rawKeyStr};
    CKeyID address = rawPubKey.GetID();
    referral::MutableReferral refNew{1, address, rawPubKey, referral::Address{}};
    refNew.signature = ParseHex("3044022068fc88103f01cf0851616131c9c83ce37c45e0392aab983980c04afa0e603bcc022043319a4e8b62456b4121e960d1b4d5ba2f29c5523e55a65da968fff27a61a321");

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nEdgeBits = nEdgeBits;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.m_vRef.push_back(referral::MakeReferralRef(std::move(refNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

    if (findPoW) {
        std::set<uint32_t> pow;

        uint32_t nMaxTries = 10000000;
        genesis.nNonce = 0;

        while (nMaxTries > 0 && !cuckoo::FindProofOfWorkAdvanced(genesis.GetHash(), genesis.nBits, genesis.nEdgeBits, pow, params)) {
            ++genesis.nNonce;
            --nMaxTries;
        }

        if (nMaxTries == 0) {
            printf("Could not find cycle for genesis block");
        } else {
            printf("Genesis block generated!!!\n");
            printf("hash: %s\nmerkelHash: %s\nnonce: %d\nedges bits: %d\naddress: %s\nnodes:\n",
                genesis.GetHash().GetHex().c_str(),
                genesis.hashMerkleRoot.GetHex().c_str(),
                genesis.nNonce,
                genesis.nEdgeBits,
                CMeritAddress(address, pkPrefix).ToString().c_str()); // use pkPrefix here as it differs for different nets
            for (const auto& node : pow) {
                printf("0x%x, ", node);
            }
        }

        exit(1);
    }

    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(
    uint32_t nTime,
    uint32_t nNonce,
    uint32_t nBits,
    uint8_t nEdgeBits,
    int32_t nVersion,
    const CAmount& genesisReward,
    Consensus::Params& params,
    addressPrefix pkPrefix,
    bool findPoW = false)
{
    const char* pszTimestamp = "Financial Times 22/Aug/2017 Globalisation in retreat: capital flows decline";
    const CScript genesisOutputScript = CScript() << ParseHex("04a7ebdbbf69ac3ea75425b9569ebb5ce22a7c277fd958044d4a185ca39077042bab520f31017d1de5c230f425cc369d5b57b66a77b983433b9b651c107aef4e35") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nEdgeBits, nVersion, genesisReward, params, pkPrefix, findPoW);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

// TODO: remove befor launch
void runEdgeBitsGenerator(Consensus::Params& consensus, addressPrefix pkPrefix)
{
    std::vector<uint8_t> bits(16);
    std::iota(std::begin(bits), std::end(bits), 16);

    printf(" EB  / Nonce /    Time    /    TPA    /                              Header\n");
    printf("========================================================================================================\n");

    std::vector<double> times;

    for (const auto& edgeBits : bits) {
        auto genesis = CreateGenesisBlock(1503444726, 0, 0x207fffff, edgeBits, 1, 50 * COIN, consensus, pkPrefix);
        std::set<uint32_t> pow;

        uint32_t nMaxTries = 10000000;

        bool found = false;
        std::vector<double> times;

        while (nMaxTries > 0 && !found) {
            auto start = std::chrono::system_clock::now();

            found = cuckoo::FindProofOfWorkAdvanced(genesis.GetHash(), genesis.nBits, genesis.nEdgeBits, pow, consensus);

            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_seconds = end - start;
            times.push_back(elapsed_seconds.count());

            if (!found) {
                ++genesis.nNonce;
                --nMaxTries;
            }
        }

        if (nMaxTries == 0) {
            printf("Could not find cycle for genesis block");
        } else {
            double timeTaken = std::accumulate(times.begin(), times.end(), 0.0);

            printf("%3d  %5d    %8.3f     %8.3f     %s\n",
                genesis.nEdgeBits,
                genesis.nNonce,
                timeTaken,
                timeTaken / times.size(),
                genesis.GetHash().GetHex().c_str());
        }
    }
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        strNetworkID = "main";
        consensus.nBlocksToMaturity = 100;
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.sEdgeBitsAllowed = {26, 27, 28, 29, 30, 31};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 4;      // adjust nEdgeBits if block time is 4x more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;        // one minute for a block
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1368; // 95% of 2016
        consensus.nMinerConfirmationWindow = 1440;       // nPowTargetTimespan / nPowTargetSpacing
        consensus.ambassador_percent_cut = 35;           //35%
        consensus.total_winning_ambassadors = 5;
        consensus.nCuckooProofSize = 42;

        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nTimeout = 1230767999;   // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000723d3581fe1bd55373540a");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000000003b9ce759c2a087d52abc4266f8f4ebd6d768b89defa50a"); //477890

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;
        nDefaultPort = 8445;
        nPruneAfterHeight = 100000;
        nMiningBlockStaleTime = 60;

        base58Prefixes[PUBKEY_ADDRESS] = addressPrefix(1, 0);
        base58Prefixes[SCRIPT_ADDRESS] = addressPrefix(1, 5);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = addressPrefix(1, 8);
        base58Prefixes[SECRET_KEY] = addressPrefix(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // genesis ref address: 13f4j1zWmweBVSQM9Jcr18JUdmJMR6kGSY
        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);
        genesis = CreateGenesisBlock(1503515697, 17, 0x207fffff, 27, 1, 50 * COIN, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x13297d, 0x2d2486, 0x58bbb7, 0xe735b1, 0x12b9fbe, 0x1efe3f7, 0x207cbc1, 0x293afb5, 0x2a14b2b,
            0x2fbca6c, 0x328abac, 0x34854e7, 0x3579a00, 0x40be381, 0x43319b5, 0x45b79ae, 0x488a33f, 0x4a0876a,
            0x4a577b9, 0x4a8d4c2, 0x4bbb34b, 0x4de1555, 0x505710e, 0x53f19bc, 0x544e889, 0x5aa884f, 0x5b1b095,
            0x5d73be9, 0x650305a, 0x6589ecc, 0x66759a9, 0x673fe32, 0x6740938, 0x6765f4b, 0x6e062b7, 0x6e5bbdb,
            0x6e5f96b, 0x74b7970, 0x7537036, 0x7b7ca61, 0x7e11d42, 0x7f9d19a};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("f8404dc84d98a1d6ea90dba88c72be91f245a585c68fe676a32c212e8cb72c44"));
        assert(genesis.hashMerkleRoot == uint256S("3a1633942793fb3e0ae37790ecd25d26a7229939c07125c67a9bdf992ef28ad9"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        /*vSeeds.emplace_back("seed.merit.sipa.be", true); // Pieter Wuille, only supports x1, x5, x9, and xd
        vSeeds.emplace_back("dnsseed.bluematt.me", true); // Matt Corallo, only supports x9
        vSeeds.emplace_back("dnsseed.merit.dashjr.org", false); // Luke Dashjr
        vSeeds.emplace_back("seed.meritstats.com", true); // Christian Decker, supports x1 - xf
        vSeeds.emplace_back("seed.merit.jonasschnelli.ch", true); // Jonas Schnelli, only supports x1, x5, x9, and xd
        vSeeds.emplace_back("seed.MRT.petertodd.org", true); // Peter Todd, only supports x1, x5, x9, and xd*/

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = (CCheckpointData){
            {
                {0, uint256S("e69d09e1479a52cf739ba605a05d5abc85b0a70768b010d3f2c0c84fe75f2cef")},
            }};

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        strNetworkID = "test";
        consensus.nBlocksToMaturity = 5;
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.sEdgeBitsAllowed = {20, 21, 22, 23, 24, 25, 26};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        // TODO: reset after testing
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 4;      // adjust nEdgeBits if block time is twice more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;        // one minute for a block
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1080; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1440;       // nPowTargetTimespan / nPowTargetSpacing
        consensus.ambassador_percent_cut = 35;           // 35%
        consensus.total_winning_ambassadors = 5;
        consensus.nCuckooProofSize = 42;

        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nTimeout = 1230767999;   // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("14933df1e491d761a3972449bc88f3525f2081060af8534f8e54ad8d793f61b0"); //1135275

        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        nDefaultPort = 18445;
        nPruneAfterHeight = 1000;
        nMiningBlockStaleTime = 60;

        base58Prefixes[PUBKEY_ADDRESS] = addressPrefix(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = addressPrefix(1, 196);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = addressPrefix(1, 150);
        base58Prefixes[SECRET_KEY] = addressPrefix(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // TODO: remove after miner is stable
        if (gArgs.GetBoolArg("-testedgebits", false)) {
            runEdgeBitsGenerator(consensus, base58Prefixes[PUBKEY_ADDRESS] = addressPrefix(1, 111));
            exit(0);
        }

        // genesis ref address: miB2255Vay5SGYsxrsbDq3WoVku4LJiFeG
        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);
        genesis = CreateGenesisBlock(1503444726, 136, 0x207fffff, 24, 1, 50 * COIN, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x64625, 0xbea75, 0xd1621, 0xda2e0, 0x18a1d7, 0x1f0a89, 0x27b01a, 0x34bf50, 0x36d0eb,
            0x48456b, 0x48fe99, 0x5166ec, 0x527c53, 0x52a05f, 0x5ae690, 0x5fe675, 0x61aa69,
            0x66b28a, 0x6a042a, 0x6c6dde, 0x72d941, 0x7711eb, 0x7bd505, 0x7f1695, 0x81ca1d,
            0x863e65, 0x921e9d, 0x940913, 0xa1197c, 0xa20a21, 0xacb91e, 0xaf3cac, 0xafcbff,
            0xb24be3, 0xbb9b6e, 0xd12fc2, 0xd7c9ef, 0xe2700a, 0xe2b5db, 0xeb81e8, 0xf6c7d4, 0xf8bb1c};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("d22a75d84d3a827245c0fe96be6eb4b63807a4acb54a6bddfd7fb717c4c588a3"));
        assert(genesis.hashMerkleRoot == uint256S("3a1633942793fb3e0ae37790ecd25d26a7229939c07125c67a9bdf992ef28ad9"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        /*vSeeds.emplace_back("testnet-seed.merit.jonasschnelli.ch", true);
        vSeeds.emplace_back("seed.tMRT.petertodd.org", true);
        vSeeds.emplace_back("testnet-seed.bluematt.me", false);
        vSeeds.emplace_back("testnet-seed.merit.schildbach.de", false);*/

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;


        checkpointData = (CCheckpointData){
            {
                {0, uint256S("0ba35302cc5c429b42e0e3729628058a6719ff2126fbd8aeea7b5d3a1c4d92e0")},
            }};

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        strNetworkID = "regtest";
        consensus.nBlocksToMaturity = 5;
        consensus.nSubsidyHalvingInterval = 15000;
        consensus.sEdgeBitsAllowed = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 2;      // adjust nEdgeBits if block time is twice more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;        // one minute for a block
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144;       // Faster than normal for regtest (144 instead of 2016)
        consensus.ambassador_percent_cut = 35;          // 35%
        consensus.total_winning_ambassadors = 5;
        consensus.nCuckooProofSize = 42;

        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18556;
        nPruneAfterHeight = 1000;
        nMiningBlockStaleTime = 60;

        base58Prefixes[PUBKEY_ADDRESS] = addressPrefix(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = addressPrefix(1, 196);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = addressPrefix(1, 150);
        base58Prefixes[SECRET_KEY] = addressPrefix(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);

        genesis = CreateGenesisBlock(1503670484, 132, 0x207fffff, 18, 1, 50 * COIN, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x12c2, 0x18c1, 0x67c7, 0x7dc6, 0x8fc0, 0x981b, 0x9fb6, 0xcc92, 0xcffe,
            0xf0f4, 0x11a50, 0x12e19, 0x16f4e, 0x19457, 0x1a6e7, 0x1e844, 0x20938,
            0x2106f, 0x244b9, 0x251eb, 0x26421, 0x268b1, 0x29219, 0x2a429, 0x2ae7e,
            0x2bd64, 0x2cf5c, 0x2d2b4, 0x2d9fa, 0x2eb9b, 0x30d6f, 0x3131b, 0x31cdb,
            0x32615, 0x32903, 0x34356, 0x3690f, 0x3839c, 0x383c7, 0x3844a, 0x3b59b,
            0x3bcb2
        };

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("90b7c7335a900467754bd7cfef641d8148ed893674f7b9d34b7046ac0eb3cde3"));
        assert(genesis.hashMerkleRoot == uint256S("3a1633942793fb3e0ae37790ecd25d26a7229939c07125c67a9bdf992ef28ad9"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = (CCheckpointData){
            {
                {0, uint256S("a0f73c7161105ba136853e99d18a4483b6319620d53adc1d14128c00fdc2d272")},
            }};

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams& Params()
{
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
