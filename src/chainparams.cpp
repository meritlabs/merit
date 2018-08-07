// Copyright (c) 2017-2018 The Merit Foundation developers
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
#include "versionbits.h"

#include <assert.h>

#include "chainparamsseeds.h"
#include "cuckoo/miner.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <time.h>
#include <vector>

using AddressPrefix = std::vector<unsigned char>;
using PubKeys = std::vector<CPubKey>;


namespace {
    const char* TIMESTAMP_MESSAGE = "Financial Times 22/Aug/2017 Globalisation in retreat: capital flows decline";
    const bool VALIDATE = true;
    const bool DONT_VALIDATE = false;
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
    const PubKeys& genesisKeys,
    const std::string& signatureHex,
    const char* pszTimestamp,
    uint32_t nTime,
    uint32_t nNonce,
    uint32_t nBits,
    uint8_t nEdgeBits,
    int32_t nVersion,
    const CAmount& genesisReward,
    Consensus::Params& params)
{
    assert(genesisKeys.size() > 1);

    const CScript redeemScript = GetScriptForMultisig(genesisKeys.size(), genesisKeys);
    const auto redeemAddress = GetScriptForDestination(CScriptID(redeemScript));

    referral::MutableReferral mutRef{2, CScriptID{redeemScript}, genesisKeys[0], referral::Address{}};
    mutRef.signature = ParseHex(signatureHex);

    referral::Referral ref{mutRef};

    const CMeritAddress address{2, ref.GetAddress()};

    const auto genesisOutputScript = GetScriptForDestination(address.Get());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig =
        CScript() << 486604799
                  << CScriptNum(4)
                  << std::vector<unsigned char>(
                          (const unsigned char*)pszTimestamp,
                          (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    // compressed pubkey

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nEdgeBits = nEdgeBits;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.m_vRef.push_back(referral::MakeReferralRef(ref));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

    return genesis;
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
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
        snapshot_url = "https://snapshot.merit.me/mainnet/current";
        consensus.nBlocksToMaturity = 100;
        consensus.initial_block_reward = 20_merit;
        consensus.nSubsidyHalvingInterval = 2102400;
        consensus.sEdgeBitsAllowed = {26, 27, 28, 29, 30, 31};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 4;      // adjust nEdgeBits if block time is 4x more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;        // one minute for a block

        const double blocks_per_minute = 60.0 / consensus.nPowTargetSpacing;

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

        consensus.daedalus_max_invites_per_block = 10; //20 merit over 2
        consensus.daedalus_block_window = blocks_per_minute  * 60 * 24 * 3; //Window used to compute invites.
                                                       //Looks at blocks over a 3 day period.
        consensus.daedalus_min_one_invite_for_every_x_blocks = 10; //Minimum of 1 invite every 10 minutes, or 144 per day.
        consensus.daedalus_max_outstanding_invites_per_address = 500;

        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].start_block = 49000; // About Feb 2, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].end_block = std::numeric_limits<int>::max();

        // Block where safer aliases are enabled.
        consensus.safer_alias_blockheight = 94200;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000000003b9ce759c2a087d52abc4266f8f4ebd6d768b89defa50a"); //477890

        // Block where improved invites turn on
        consensus.imp_invites_blockheight = 180980; //Sat, Jan 5th 6:11:00 ish GMT
        consensus.imp_block_window = blocks_per_minute  * 60 * 24 * 1; //1 days of blocks
        consensus.imp_min_one_invite_for_every_x_blocks = 10; //invite every 10 minutes at a minumum, 144 per day.
        consensus.imp_miner_reward_for_every_x_blocks = 10; //invite every 10 minutes at a minumum, 144 per day.
        consensus.imp_weights = {60, 40}; 

        // PoG v2 settings.
        consensus.pog2_blockheight = 400000;
        consensus.pog2_total_winning_ambassadors = 15;
        consensus.pog2_ambassador_percent_cut = 50;  //50%
        consensus.pog2_pow_target_timespan = 5 * 60 * 60; // every 5 hours
        consensus.pog2_initial_ambassador_stake = 20_merit;
        consensus.pog2_coin_maturity = blocks_per_minute * 60 * 24 * 30; //Month for coin maturity
        consensus.pog2_new_coin_maturity = blocks_per_minute * 60 * 24 * 30; //Month for coin maturity
        consensus.pog2_max_outstanding_invites_per_address = 50;
        consensus.pog2_convex_b = 0.2; 
        consensus.pog2_convex_s = 0.05;

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
        nMiningBlockStaleTime = 35;

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 50);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 63);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 56);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // Note that of those with the service bits flag, most only support a subset of possible options
        vSeeds.clear();
        vSeeds.emplace_back("seed.merit.me", false);
        vSeeds.emplace_back("seed.brightkey.io", false);
        vSeeds.emplace_back("seed.adil.io", false);
        vSeeds.emplace_back("seed.lvlup.com", false);
        vSeeds.emplace_back("merit.crypvix.com", false);
        vSeeds.emplace_back("seeds.merit.blockcasts.com", false);
        vSeeds.emplace_back("merit.commerce6.com", false);
        vSeeds.emplace_back("seeds.venturebot.us", false);
        vSeeds.emplace_back("seed.firstpri.me", false);
        vSeeds.emplace_back("seeds.merit.rocks", false);


        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = CCheckpointData {
            {
                {49000, {uint256S("a133b70798599e3c9edd67def448d2df0ce2326e70aae8035329550650fb93c6"), VALIDATE}},
                //==================================================
                //These checkpoints are to work around an invite and ambassador lottery bug. The resulting
                //distribution is still random but the exact payout was different. These checkpoints
                //make sure users can continue down the longest chain.
                {74924, {uint256S("05184d173389688471b51ded4a1bbcffe819d976ba7b2b210de1b9dd455b136a"), DONT_VALIDATE}},
                {83566, {uint256S("395e9645213884ab91e1eade234ba78edfcdc5e6ddb9182b7dd61259da6e34f2"), DONT_VALIDATE}},
                {87887, {uint256S("2131c1c464642d73504541dfd24ded42d8942de923345ab309397aba25b91527"), DONT_VALIDATE}},
                {92208, {uint256S("1b865121cc2b7b8c730a46d42faea035646897f208098437dc9284d707955b99"), DONT_VALIDATE}},
                {96529, {uint256S("599f23553592401a61813cfd144e0a75e798fd01c8b4268855b1880d71bc320f"), DONT_VALIDATE}},
                {97589, {uint256S("17a0a02de5ccfd673d7df5bac6bda4fba7b869cda6bce51cf0f6a93befa505ab"), DONT_VALIDATE}},
                {123498,{uint256S("5d458d6afed5a9fee43b0281e3b2e8717dfc5a1c9cd1ceba345131b290530637"), DONT_VALIDATE}},
                //==================================================
            }};

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }

    void Init() override
    {
        CAmount genesisReward = 20000000_merit;

        PubKeys genesisKeys = {
            CPubKey{ParseHex("02DB1B668505E835356B3CC854B4F04CF94812E0CB536AD7E13D6C32E5441C901C")},
            CPubKey{ParseHex("033743F618164114D64845BEE3947DDA816A833F69FD996586738D57DF32B5C878")},
        };

        const std::string referralSig = "3044022075966858282b5f174348becf2b36e7474fe981c4d99d6d826fafe9d0ac24e8e102202b934185ebcd218479db27e4af0a7c30ad9c60e9d04f16e9e21884b8275e4623";

        // genesis ref address: ST2HYE5KMszAdBcGo3kw7Qsb9u1nRQhac4
        consensus.genesis_address = uint160{ParseHex("3ed7e0dbbe7d8ae8f478cb69bea2edf878760d74")};

        genesis = CreateGenesisBlock(genesisKeys, referralSig, TIMESTAMP_MESSAGE, 1514332800,  1, 0x207fffff, 27, 1, genesisReward, consensus);

        genesis.sCycle = {
            0x15d885, 0x256dce, 0x2cc8d0, 0x5cd44a, 0xd6d132, 0x106b67b, 0x11962db, 0x14ab89d, 0x18abdce, 0x1a45363, 0x1a7f63b, 0x1bbd6a5, 0x1bf9e06, 0x1c5867a, 0x20ad7f3, 0x24e9681, 0x24fb531, 0x29fe5c4, 0x2aaf2d5, 0x362d3ff, 0x39fc056, 0x3fc1e9a, 0x4c15367, 0x4e7fd5a, 0x5021fd5, 0x50cbb61, 0x5213f29, 0x55ca2e7, 0x594706d, 0x5b74b85, 0x5dc54ba, 0x5f02c74, 0x651ab75, 0x66627a8, 0x672d4a5, 0x69030db, 0x6b7dd35, 0x6ccbc8c, 0x77c92c1, 0x77e766a, 0x7a30059, 0x7d86a68,
        };

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("5fe9fb4f6bb108383e61cf4401dff6e947f6345956bf2f54b19ffd1092028c24"));
        assert(genesis.hashMerkleRoot == uint256S("61621466cfa6f549f5dbc144057d96046989f830c7bff2743e593a161ba42499"));
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
        consensus.initial_block_reward = 20_merit;
        consensus.nSubsidyHalvingInterval = 2102400;
        consensus.sEdgeBitsAllowed = {20, 21, 22, 23, 24, 25, 26};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        // TODO: reset after testing
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day for nBits adjustment
        consensus.nEdgeBitsTargetThreshold = 4;      // adjust nEdgeBits if block time is twice more/less than expected
        consensus.nPowTargetSpacing = 1 * 60;        // one minute for a block
        const double blocks_per_minute = 60.0 / consensus.nPowTargetSpacing;

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

        consensus.daedalus_max_invites_per_block = 10;
        consensus.daedalus_block_window = 4;
        consensus.daedalus_min_one_invite_for_every_x_blocks = 1;
        consensus.daedalus_max_outstanding_invites_per_address = 3;

        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].start_block = 500;
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].end_block = std::numeric_limits<int>::max();

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("14933df1e491d761a3972449bc88f3525f2081060af8534f8e54ad8d793f61b0"); //1135275

        // Block where safer aliases are enabled.
        consensus.safer_alias_blockheight = 2060;

        // Block where improved invites turn on
        consensus.imp_invites_blockheight = 82275;
        consensus.imp_block_window = 4;
        consensus.imp_min_one_invite_for_every_x_blocks = 2; //invite every minute
        consensus.imp_miner_reward_for_every_x_blocks = 10; //invite every 10 minutes at a minumum, 144 per day.
        consensus.imp_weights = {60, 40}; 

        // PoG v2 settings.
        consensus.pog2_blockheight = 216810;
        consensus.pog2_total_winning_ambassadors = 15;
        consensus.pog2_ambassador_percent_cut = 50;  //50%
        consensus.pog2_pow_target_timespan = 5 * 60 * 60; // every 5 hours
        consensus.pog2_initial_ambassador_stake = 20_merit;
        consensus.pog2_coin_maturity = blocks_per_minute * 60 * 24 * 30; //Month for coin maturity
        consensus.pog2_new_coin_maturity = blocks_per_minute * 60 * 24 * 30; //Month for coin maturity
        consensus.pog2_max_outstanding_invites_per_address = 50;
        consensus.pog2_convex_b = 0.2; 
        consensus.pog2_convex_s = 0.05;

        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        nDefaultPort = 18445;
        nPruneAfterHeight = 1000;
        nMiningBlockStaleTime = 35;

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 110);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 125);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 117);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        vFixedSeeds.clear();
        vSeeds.clear();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }

    void Init() override
    {
        CAmount genesisReward = 20000000_merit;

        // genesis ref address:
        // sPm5Tq6pZwDtcgGMJcqsvtmh5wZsSqVyRH
        consensus.genesis_address = uint160{ParseHex("3c759153e6519361689f43d1ed981c1417c05dcf")};


        PubKeys genesisKeys{
            CPubKey(ParseHex("03C710FD3FD8B56537BF121870AF462107D3583F7E0CBD97F80EE271F48DAFF593")),
                CPubKey(ParseHex("024F1BC2E023ED1BACDC8171798113F1F7280C881919A11B592A25A976ABFB8798")),
        };

        const std::string referralSig =
            "304502210090792fc651c1d88caf78a071b9a33699e9f2324af3096d45e6c7a3"
            "bd1e4ec39902202d4b5ac449d94b49b308f7faf42a2f624b3cc4f1569b7621e9"
            "f967f5b6895626";

        genesis = CreateGenesisBlock(genesisKeys, referralSig, TIMESTAMP_MESSAGE, 1514332800, 381, 0x207fffff, 24, 1, genesisReward, consensus);

        genesis.sCycle = {
            0x13529, 0xb3ef1, 0xf3211, 0x166f1d, 0x1fe182, 0x229740, 0x2704c2, 0x2a3b1b,
            0x32053c, 0x39fee1, 0x3ed8ff, 0x3f079d, 0x408b98, 0x40b31d, 0x434ea2, 0x463eaa,
            0x482bb4, 0x49eae3, 0x4bb609, 0x545752, 0x5a2d5b, 0x5e3999, 0x6ca1d2, 0x76c4f7,
            0x826245, 0x82d44d, 0xad2cd4, 0xafd7be, 0xb5792b, 0xb593a2, 0xb7f4fb, 0xc2a540,
            0xcec41e, 0xd33967, 0xdbb0b8, 0xdc9ce4, 0xdf509e, 0xe04520, 0xe187ef, 0xe30157,
            0xed068f, 0xfd58fe
        };

        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("448f31e47f5daabfd1984f03a64723c7f50b2306961e6f0e7f482e0b49f2dbea"));
        assert(genesis.hashMerkleRoot == uint256S("8be99a68b2514e86f17368e9cce63d302aa0f29ed91654b7c90dc9f7201fb69f"));
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
        consensus.initial_block_reward = 20_merit;
        consensus.nSubsidyHalvingInterval = 2102400;
        consensus.sEdgeBitsAllowed = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
        consensus.powLimit = Consensus::PoWLimit{
            uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
            *consensus.sEdgeBitsAllowed.begin()};
        consensus.nPowTargetTimespan = 60; // one minute for nBits adjustment
        consensus.nPowTargetSpacing = 10;        // 10 seconds per block

        const double blocks_per_minute = 60.0 / consensus.nPowTargetSpacing;

        consensus.nEdgeBitsTargetThreshold = 2;      // adjust nEdgeBits if block time is twice more/less than expected
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

        consensus.daedalus_max_invites_per_block = 10;
        consensus.daedalus_block_window = 4;
        consensus.daedalus_min_one_invite_for_every_x_blocks = 1;
        consensus.daedalus_max_outstanding_invites_per_address = 100;

        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].start_block = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].end_block = std::numeric_limits<int>::max();

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Block where safer aliases are enabled.
        consensus.safer_alias_blockheight = 5;

        // Block where improved invites turn on
        consensus.imp_invites_blockheight = 8;
        consensus.imp_block_window = 4;
        consensus.imp_min_one_invite_for_every_x_blocks = 1; //invite every minute
        consensus.imp_miner_reward_for_every_x_blocks = 10; //invite every 10 minutes at a minumum, 144 per day.
        consensus.imp_weights = {60, 40}; 

        // PoG v2 settings.
        consensus.pog2_blockheight = 10;
        consensus.pog2_total_winning_ambassadors = 15;
        consensus.pog2_ambassador_percent_cut = 50;  //50%
        consensus.pog2_pow_target_timespan = 30 * 60; // every 30 min
        consensus.pog2_initial_ambassador_stake = 20_merit;
        consensus.pog2_coin_maturity = blocks_per_minute * 20; //20 minutes for coin maturity
        consensus.pog2_new_coin_maturity = blocks_per_minute * 10; //10 minutes for coin maturity
        consensus.pog2_max_outstanding_invites_per_address = 50;
        consensus.pog2_convex_b = 0.2; 
        consensus.pog2_convex_s = 0.05;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 18556;
        nPruneAfterHeight = 1000;
        nMiningBlockStaleTime = 35;

        base58Prefixes[PUBKEY_ADDRESS] = AddressPrefix(1, 110);
        base58Prefixes[SCRIPT_ADDRESS] = AddressPrefix(1, 125);
        base58Prefixes[PARAM_SCRIPT_ADDRESS] = AddressPrefix(1, 117);
        base58Prefixes[SECRET_KEY] = AddressPrefix(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        chainTxData = ChainTxData{
            0,
            0,
            0};
    }

    void Init() override
    {
        CAmount genesisReward = 20000000_merit;

        // genesis ref address:
        // sPm5Tq6pZwDtcgGMJcqsvtmh5wZsSqVyRH
        consensus.genesis_address = uint160{ParseHex("3c759153e6519361689f43d1ed981c1417c05dcf")};


        PubKeys genesisKeys{
            CPubKey(ParseHex("03C710FD3FD8B56537BF121870AF462107D3583F7E0CBD97F80EE271F48DAFF593")),
                CPubKey(ParseHex("024F1BC2E023ED1BACDC8171798113F1F7280C881919A11B592A25A976ABFB8798")),
        };

        const std::string referralSig =
            "304502210090792fc651c1d88caf78a071b9a33699e9f2324af3096d45e6c7a3"
            "bd1e4ec39902202d4b5ac449d94b49b308f7faf42a2f624b3cc4f1569b7621e9"
            "f967f5b6895626";

        genesis = CreateGenesisBlock(genesisKeys, referralSig, TIMESTAMP_MESSAGE, 1514332800, 381, 0x207fffff, 24, 1, genesisReward, consensus);

        genesis.sCycle = {
            0x13529, 0xb3ef1, 0xf3211, 0x166f1d, 0x1fe182, 0x229740, 0x2704c2, 0x2a3b1b,
            0x32053c, 0x39fee1, 0x3ed8ff, 0x3f079d, 0x408b98, 0x40b31d, 0x434ea2, 0x463eaa,
            0x482bb4, 0x49eae3, 0x4bb609, 0x545752, 0x5a2d5b, 0x5e3999, 0x6ca1d2, 0x76c4f7,
            0x826245, 0x82d44d, 0xad2cd4, 0xafd7be, 0xb5792b, 0xb593a2, 0xb7f4fb, 0xc2a540,
            0xcec41e, 0xd33967, 0xdbb0b8, 0xdc9ce4, 0xdf509e, 0xe04520, 0xe187ef, 0xe30157,
            0xed068f, 0xfd58fe
        };

        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("448f31e47f5daabfd1984f03a64723c7f50b2306961e6f0e7f482e0b49f2dbea"));
        assert(genesis.hashMerkleRoot == uint256S("8be99a68b2514e86f17368e9cce63d302aa0f29ed91654b7c90dc9f7201fb69f"));
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
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
    globalChainParams->Init();
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
