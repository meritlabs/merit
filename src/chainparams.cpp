// Copyright (c) 2013-2017 The Merit Foundation developers
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
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
    bool findPoW)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    auto rawKeyStr = ParseHex("04a7ebdbbf69ac3ea75425b9569ebb5ce22a7c277fd958044d4a185ca39077042bab520f31017d1de5c230f425cc369d5b57b66a77b983433b9b651c107aef4e35");
    CPubKey rawPubKey{rawKeyStr};
    CKeyID address = rawPubKey.GetID();
    referral::MutableReferral refNew;
    refNew.codeHash.SetHex("73a50383c1e58f5f215cdb40508b584bfd9f8d0e46cc3d0f17c79c6774a5dafd");
    refNew.addressType = 1;
    refNew.pubKeyId = address;
    refNew.previousReferral.SetNull();

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
            printf("hash: %s\nmerkelHash: %s\nnonce: %d\nedges bits: %d\nnodes:\n",
                genesis.GetHash().GetHex().c_str(),
                genesis.hashMerkleRoot.GetHex().c_str(),
                genesis.nNonce,
                genesis.nEdgeBits);
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
    bool findPoW = false)
{
    const char* pszTimestamp = "Financial Times 22/Aug/2017 Globalisation in retreat: capital flows decline";
    const CScript genesisOutputScript = CScript() << ParseHex("04a7ebdbbf69ac3ea75425b9569ebb5ce22a7c277fd958044d4a185ca39077042bab520f31017d1de5c230f425cc369d5b57b66a77b983433b9b651c107aef4e35") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nEdgeBits, nVersion, genesisReward, params, findPoW);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

// TODO: remove befor launch
void runEdgeBitsGenerator(Consensus::Params& consensus)
{
    std::vector<uint8_t> bits(16);
    std::iota(std::begin(bits), std::end(bits), 16);

    printf(" EB  / Nonce /    Time    /    TPA    /                              Header\n");
    printf("========================================================================================================\n");

    std::vector<double> times;

    for (const auto& edgeBits : bits) {
        auto genesis = CreateGenesisBlock(1503444726, 0, 0x207fffff, edgeBits, 1, 50 * COIN, consensus);
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

        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);
        genesis = CreateGenesisBlock(1503515697, 131, 0x207fffff, 28, 1, 50 * COIN, consensus, generateGenesis);

        genesis.sCycle = {0x2077a, 0x4cbf3b, 0x60b30c, 0x6ff5d8, 0x992011, 0xb805cd, 0xbc47eb, 0xbf5169, 0xc1918c,
            0xe87071, 0xfac34a, 0x1145fcb, 0x14c597e, 0x155646c, 0x174d8d0, 0x18b83c6, 0x19fd75a, 0x1a12b40, 0x1a7637e,
            0x1adadd9, 0x1c0994f, 0x1e007ad, 0x22a00a2, 0x2374c5e, 0x276f9f4, 0x27910f8, 0x286c27a, 0x2a6f7c5, 0x2aee0e6,
            0x2b6182f, 0x2c9174d, 0x2cc3922, 0x305c560, 0x340d0de, 0x34f3cc5, 0x36be4cd, 0x390c947, 0x3a90c9c, 0x3d40295,
            0x3e31d30, 0x3e32e42, 0x3fe989b};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("e69d09e1479a52cf739ba605a05d5abc85b0a70768b010d3f2c0c84fe75f2cef"));
        assert(genesis.hashMerkleRoot == uint256S("12f0ddebc1f8d0d24487ccd1d21bfd466a298e887f10bb0385378ba52a0b875c"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        /*vSeeds.emplace_back("seed.merit.sipa.be", true); // Pieter Wuille, only supports x1, x5, x9, and xd
        vSeeds.emplace_back("dnsseed.bluematt.me", true); // Matt Corallo, only supports x9
        vSeeds.emplace_back("dnsseed.merit.dashjr.org", false); // Luke Dashjr
        vSeeds.emplace_back("seed.meritstats.com", true); // Christian Decker, supports x1 - xf
        vSeeds.emplace_back("seed.merit.jonasschnelli.ch", true); // Jonas Schnelli, only supports x1, x5, x9, and xd
        vSeeds.emplace_back("seed.MRT.petertodd.org", true); // Peter Todd, only supports x1, x5, x9, and xd*/

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 5);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 8);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

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
        consensus.nPowTargetTimespan = 20 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 4; // adjust nEdgeBits if block time is twice more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;   // one minute for a block
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 45; // 75% for testchains
        consensus.nMinerConfirmationWindow = 60;         // nPowTargetTimespan / nPowTargetSpacing
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

        // TODO: remove after miner is stable
        if (gArgs.GetBoolArg("-testedgebits", false)) {
            runEdgeBitsGenerator(consensus);
            exit(0);
        }

        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);
        genesis = CreateGenesisBlock(1503444726, 16, 0x207fffff, 24, 1, 50 * COIN, consensus, generateGenesis);

        genesis.sCycle = {
            0x2e65e, 0x2ef31, 0xe8ca8, 0xe9443, 0x1cca67, 0x1dae0f, 0x1e4e40, 0x20496c, 0x265b10, 0x28a207,
            0x31afc5, 0x40e747, 0x4f28a2, 0x5118af, 0x58988d, 0x5928df, 0x5f3c30, 0x6357bb, 0x682380,
            0x712045, 0x715d03, 0x797191, 0x8460e8, 0x97416d, 0x982c36, 0x9940e9, 0x9fe403, 0xa13dfa,
            0xa3ba51, 0xa4b570, 0xa77e52, 0xabbbb7, 0xae519d, 0xbbc128, 0xc4116c, 0xcf822d, 0xd5c7ee,
            0xdef513, 0xe5c07a, 0xf0e870, 0xf266c6, 0xf3c0a9};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("3ebaabc77fe92523326e0d2de5c3f3cac3969690896451aed4a9683aa8c89fb5"));
        assert(genesis.hashMerkleRoot == uint256S("cfee6b4b3d9bf62a5c6762468879a66ab1c2038b59eaebf14db51a2e17ac8414"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        /*vSeeds.emplace_back("testnet-seed.merit.jonasschnelli.ch", true);
        vSeeds.emplace_back("seed.tMRT.petertodd.org", true);
        vSeeds.emplace_back("testnet-seed.bluematt.me", false);
        vSeeds.emplace_back("testnet-seed.merit.schildbach.de", false);*/
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 150);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

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

        bool generateGenesis = gArgs.GetBoolArg("-generategenesis", false);

        genesis = CreateGenesisBlock(1503670484, 2, 0x207fffff, 18, 1, 50 * COIN, consensus, generateGenesis);

        genesis.sCycle = {0xff, 0x3b5, 0x8e5, 0xa39, 0xf5b, 0xfd2, 0x15ad, 0x1a85, 0x2964, 0x2b43, 0x356f, 0x4f10,
            0x5c0e, 0x5ef9, 0x686f, 0x6e9a, 0x749e, 0x7708, 0x7f2a, 0x8a6d, 0x8e09, 0x902c, 0x9278, 0x94c3, 0x9d99,
            0xa1a8, 0xa2e0, 0xab0b, 0xafb4, 0xb440, 0xd302, 0xd604, 0xdc5e, 0xe6cd, 0xea2b, 0xefda, 0xf094, 0xf451,
            0x10550, 0x106f6, 0x108c0, 0x113a3};

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("1b406b3f7eba08bc4dbe66b00eb8c96cff485c8074f67408923f952a2115c6a3"));
        assert(genesis.hashMerkleRoot == uint256S("cfee6b4b3d9bf62a5c6762468879a66ab1c2038b59eaebf14db51a2e17ac8414"));

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

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 150);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
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
