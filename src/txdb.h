// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_TXDB_H
#define MERIT_TXDB_H

#include "coins.h"
#include "dbwrapper.h"
#include "chain.h"
#include "addressindex.h"
#include "spentindex.h"
#include "timestampindex.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewDBCursor;
class uint256;
class CChainParams;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int DB_PEAK_USAGE_FACTOR = 2;
//! No need to periodic flush if at least this much space still available.
static constexpr int MAX_BLOCK_COINSDB_USAGE = 200 * DB_PEAK_USAGE_FACTOR;
//! Always periodic flush if less than this much space still available.
static constexpr int MIN_BLOCK_COINSDB_USAGE = 50 * DB_PEAK_USAGE_FACTOR;
//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 1024;
//! -dbbatchsize default (bytes)
static const int64_t nDefaultDbBatchSize = 16 << 20;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference: https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxBlockDBAndTxIndexCache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 300;
//! Max memory allocated to referral DB specific cache (MiB)
static const int64_t nMaxReferralDBCache = 200;

extern const char DB_ADDRESSUNSPENTINDEX;

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {
    }

    CDiskTxPos() {
        SetNull();
    }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB final : public CCoinsView
{
protected:
    CDBWrapper db;
public:
    explicit CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) override;
    CCoinsViewCursor *Cursor() const override;

    //! Attempt to update from an older database format. Returns whether an error occurred.
    bool Upgrade();
    size_t EstimateSize() const override;
};

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    ~CCoinsViewDBCursor() {}

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;
    unsigned int GetValueSize() const override;

    bool Valid() const override;
    void Next() override;

private:
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256 &hashBlockIn):
        CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    explicit CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false, bool compression = false, int maxOpenFiles = 64);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool ReadSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value);
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    bool ReadAddressUnspentIndex(
            uint160 addressHash,
            unsigned int type,
            bool invite,
            std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect);
    bool ReadAllAddressUnspent(
        bool invite,
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs);

    template<class F>
        bool ReadAllAddressUnspent(
                bool invite,
                F process) {
            leveldb::ReadOptions options;
            options.fill_cache = false;
            boost::scoped_ptr<CDBIterator> pcursor(NewIterator(options));

            pcursor->Seek(DB_ADDRESSUNSPENTINDEX);

            while (pcursor->Valid()) {
                boost::this_thread::interruption_point();
                std::pair<char,CAddressUnspentKey> key;
                if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX)  {

                    if(key.second.isInvite == invite) {
                        CAddressUnspentValue value;
                        if (pcursor->GetValue(value)) {
                            process(key.second, value);
                        } else {
                            return error("failed to get address unspent value");
                        }
                    }
                }
                pcursor->Next();
            }

            return true;
        }

    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool ReadAddressIndex(
            uint160 addressHash,
            unsigned int type,
            bool invite,
            std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
            int start = 0,
            int end = 0);
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    bool ReadTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &vect);
    bool WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts);
    bool ReadTimestampBlockIndex(const uint256 &hash, unsigned int &logicalTS);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts(
            const Consensus::Params& consensusParams,
            std::function<CBlockIndex*(const uint256&)> insertBlockIndex);

    // Referrals
    bool ReadReferralIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteReferralIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
};

#endif // MERIT_TXDB_H
