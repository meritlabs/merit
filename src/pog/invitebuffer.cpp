// Copyright (c) 2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pog/invitebuffer.h"
#include "validation.h"

#include <vector>

namespace pog 
{
    InviteBuffer::InviteBuffer(const CChain& c) : chain{c} {}

    bool ComputeStats(
            const CBlock& block,
            InviteStats& stats,
            const Consensus::Params& params)
    {
        for (const auto& invite : block.invites) {
            if (!invite->IsCoinBase()) {
                for (const auto& in : invite->vin) {
                    CTransactionRef prev;
                    uint256 block_inv_is_in;

                    if (!GetTransaction(
                                in.prevout.hash,
                                prev,
                                params,
                                block_inv_is_in,
                                false)) {
                        return false;
                    }

                    assert(prev);
                    if (!prev->IsCoinBase()) {
                        continue;
                    }
                    stats.invites_used += prev->vout.at(in.prevout.n).nValue;
                }
            } else {
                for (const auto& out : invite->vout) {
                    stats.invites_created += out.nValue;
                }
            }
        }

        return true;
    }

    int AdjustedHeight(int height, const Consensus::Params& params) 
    {
        auto daedalus_start = params.vDeployments[Consensus::DEPLOYMENT_DAEDALUS].start_block;
        return std::max(0, height - daedalus_start);
    }

    InviteStats InviteBuffer::get(int height, const Consensus::Params& params) const
    {
        LOCK(cs);

        InviteStats s;

        auto adjusted_height = AdjustedHeight(height, params);

        //get cached value, otherwise we compute.
        if(get(adjusted_height, s)) {
            return s;
        }

        const auto index = chain[height];
        if(!index) {
            return s;
        }

        CBlock block;
        if (!ReadBlockFromDisk(block, index, params, false)) {
            return s;
        }

        if (!ComputeStats(block, s, params)) {
            return s;
        }

        s.is_set = true;
        insert(adjusted_height, s);
        return s;
    }

    bool InviteBuffer::get(int adjusted_height, InviteStats& s) const
    {
        if(stats.size() <= adjusted_height) {
            return false;
        }

        s = stats[adjusted_height];
        return s.is_set;
    }

    void InviteBuffer::insert(int adjusted_height, const InviteStats& s) const
    {
        if(stats.size() <= adjusted_height) {
            stats.resize(adjusted_height+1);
        }
        stats[adjusted_height] = s;
    }

} // namespace pog
