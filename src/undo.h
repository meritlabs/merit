// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERIT_UNDO_H
#define MERIT_UNDO_H

#include "compressor.h"
#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "refdb.h"

static const size_t MIN_TRANSACTION_INPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxIn(), SER_NETWORK, PROTOCOL_VERSION);
static const size_t MAX_INPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_INPUT_WEIGHT;

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<Coin> vprevout;

    template <typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, vprevout);
    }

    template <typename Stream>
    void Unserialize(Stream& s) {
        ::Unserialize(s, vprevout);
    }
};

/** Undo information for a CBlock */
struct CBlockUndo
{
    std::vector<CTxUndo> vtxundo; // for all but the coinbase
    referral::LotteryUndos lottery;
    std::vector<CTxUndo> invites_undo;

    template <typename Stream>
    void Serialize(Stream& s) const {
        s << vtxundo;

        //If we are daedalus, we will signal it by including 
        //a lottery element with an address of type 100
        if(!invites_undo.empty()) {
            WriteCompactSize(s, lottery.size() + 1);
            for(const auto& e : lottery) {
                s << e;
            }

            referral::LotteryUndo signal{
                0,
                100,
                referral::Address(),
                referral::Address()};

            s << signal;
        } else {
            s << lottery;
        }

        if(!invites_undo.empty()) {
            s << invites_undo;
        }
    }

    template <typename Stream>
    void Unserialize(Stream& s) {
        s >> vtxundo;
        s >> lottery;
        if(!lottery.empty() && lottery.back().replaced_address_type == 100) {
            lottery.resize(lottery.size()-1);
            s >> invites_undo;
        }
    }
};

#endif // MERIT_UNDO_H
