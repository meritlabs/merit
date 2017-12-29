// Copyright (c) 2013-2017 The Merit Foundation developers
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"

#include "miner.h"
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

using AddressPrefix = std::vector<unsigned char>;

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
    AddressPrefix pkPrefix,
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

        while (nMaxTries > 0 && !cuckoo::FindProofOfWorkAdvanced(genesis.GetHash(), genesis.nBits, genesis.nEdgeBits, pow, params, DEFAULT_MINING_THREADS)) {
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
    AddressPrefix pkPrefix,
    bool findPoW = false)
{
    const char* pszTimestamp = "Financial Times 22/Aug/2017 Globalisation in retreat: capital flows decline";
    const CScript genesisOutputScript = CScript() << ParseHex("0337d249c44b0327389a65687c7e9a823271a8c4355c74378d0b608b3339480e9a") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nEdgeBits, nVersion, genesisReward, params, pkPrefix, findPoW);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

// TODO: remove befor launch
void runEdgeBitsGenerator(Consensus::Params& consensus, AddressPrefix pkPrefix)
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

            found = cuckoo::FindProofOfWorkAdvanced(genesis.GetHash(), genesis.nBits, genesis.nEdgeBits, pow, consensus, DEFAULT_MINING_THREADS);

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
        consensus.initial_block_reward = 20;
        consensus.nSubsidyHalvingInterval = 2102400;
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
        consensus.max_lottery_reservoir_size = 10000;
        consensus.nCuckooProofSize = 42;

        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_GENESIS].nTimeout = 1230767999;   // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000000003b9ce759c2a087d52abc4266f8f4ebd6d768b89defa50a"); //477890

        // Genesis reward 
        CAmount genesisReward = 20000000_merit;

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

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 50);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 63);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 56);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // genesis ref address: 13f4j1zWmweBVSQM9Jcr18JUdmJMR6kGSY
        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);

        genesis = CreateGenesisBlock(1514332800, 86, 0x207fffff, 27, 1, genesisReward, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x655d62, 0xed3ff7, 0x1547118, 0x17e18e9, 0x1951b67, 0x1a62f65, 0x1d198ae, 0x1e0ba85, 0x1e62486,
            0x2089288, 0x2375a91, 0x24062c5, 0x2cbaa5b, 0x2de47f4, 0x311bf15, 0x316a6bc, 0x35d5304, 0x3c0d00a,
            0x3c886d9, 0x4083f0a, 0x4764c00, 0x4ba602d, 0x4cb39a4, 0x4e787d4, 0x502ac74, 0x53e184a, 0x55d0a7e,
            0x587c138, 0x5b048b7, 0x5b45c38, 0x5b9d7a4, 0x5f2911f, 0x629008a, 0x65320c5, 0x677d335, 0x68d242b,
            0x6af63ab, 0x6b8d144, 0x6e76030, 0x6f53051, 0x6f75c4e, 0x75d6a77};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("7d90f80b61a7b3a343194f2ff9e2f78e0230768e49e8df383ee690162ba347a8"));
        assert(genesis.hashMerkleRoot == uint256S("7464a1ce95a025602f92c5584be0904c5d9211e87dd08811f7a7774ddcbdc565"));


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
        consensus.initial_block_reward = 20;
        consensus.nSubsidyHalvingInterval = 2102400;
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
        consensus.max_lottery_reservoir_size = 100;
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

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 110);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 125);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 117);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // TODO: remove after miner is stable
        if (gArgs.GetBoolArg("-testedgebits", false)) {
            runEdgeBitsGenerator(consensus, base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 111));
            exit(0);
        }

        CAmount genesisReward = 20000000_merit;

        // genesis ref address: miB2255Vay5SGYsxrsbDq3WoVku4LJiFeG
        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);
        genesis = CreateGenesisBlock(1514332800, 96, 0x207fffff, 24, 1, genesisReward, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x6a34, 0xa9f33, 0xaef47, 0xcab32, 0x17b5a5, 0x1b10f8, 0x1e2d11, 0x2358bb, 0x2432c5,
            0x2aea48, 0x2e1a1e, 0x376b30, 0x376f1a, 0x3c1e94, 0x3f1a3d, 0x4a7b5d, 0x5205c4,
            0x55859e, 0x599c1a, 0x5b8ca9, 0x5f78e0, 0x6ed41e, 0x771733, 0x96e970, 0x9f4171,
            0x9f5497, 0xa0cd41, 0xa16a53, 0xa24199, 0xa42687, 0xa7c282, 0xb36f11, 0xb45c67,
            0xbf0600, 0xbf2c25, 0xc342b1, 0xc70509, 0xdc18e6, 0xe4e564, 0xf147e0, 0xf6bde5,
            0xfcc8ee};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("8689bfaaebb6d43bf6a5316bcc1f3ccb41aa067d842ed24587adb91ef0df8bd5"));
        assert(genesis.hashMerkleRoot == uint256S("7464a1ce95a025602f92c5584be0904c5d9211e87dd08811f7a7774ddcbdc565"));


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
        consensus.initial_block_reward = 20;
        consensus.nSubsidyHalvingInterval = 2102400;
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
        consensus.max_lottery_reservoir_size = 100;
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

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 110);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 125);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 117);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);

        CAmount genesisReward = 20000000_merit;
        genesis = CreateGenesisBlock(1514332800, 13, 0x207fffff, 18, 1, genesisReward, consensus, base58Prefixes[PUBKEY_ADDRESS], generateGenesis);

        genesis.sCycle = {
            0x2be5, 0x30b9, 0x36d1, 0x5f03, 0x8abd, 0xc678, 0x10d8c, 0x11727, 0x11728, 0x12518, 0x127f2, 0x15b8e,
            0x16bb7, 0x17c6f, 0x18282, 0x184a0, 0x187df, 0x19aa0, 0x1aa07, 0x1aaf1, 0x1bdda, 0x1d49f, 0x1dd18,
            0x21c64, 0x23436, 0x23e39, 0x24a06, 0x25d98, 0x28687, 0x2d58e, 0x2e9a8, 0x2f202,0x2f412, 0x2fc95,
            0x31d70, 0x3265a, 0x32b6e, 0x37500, 0x3a8b5, 0x3e37a, 0x3f2ef, 0x3f908
        };

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("d32c0f46ee271b361562a1f24f6e852dc4fe66493ae5acf58cd8db3415061339"));
        assert(genesis.hashMerkleRoot == uint256S("7464a1ce95a025602f92c5584be0904c5d9211e87dd08811f7a7774ddcbdc565"));

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
