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
            int height,
            const CBlock& block,
            InviteStats& stats,
            const Consensus::Params& params)
    {
        assert(height >= 0);

        bool check_for_beacon = height >= params.imp_invites_blockheight;

        std::set<uint160> beaconed_addresses;
        if(check_for_beacon) {
            for(const auto& beacon : block.m_vRef) {
                beaconed_addresses.insert(beacon->GetAddress());
            }
        }

        for (const auto& invite : block.invites) {
            if (!invite->IsCoinBase()) {

                int coinbase_used = 0;
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

                    coinbase_used += prev->vout.at(in.prevout.n).nValue;
                }

                if (check_for_beacon) {
                    int beacons_invited = 0;
                    for (const auto& out: invite->vout) {
                        const auto addr = ExtractAddress(out);
                        if(addr.second == 0) {
                            continue;
                        }
                        beacons_invited += beaconed_addresses.count(addr.first);
                    }

                    stats.invites_used += std::min(coinbase_used, beacons_invited);
                    stats.invites_used_fixed += beacons_invited;

                } else {
                    stats.invites_used += coinbase_used;
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

        const auto adjusted_height = AdjustedHeight(height, params);

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

        if (!ComputeStats(height, block, s, params)) {
            return s;
        }

        s.is_set = true;
        insert(adjusted_height, s);
        return s;
    }

    bool InviteBuffer::set_mean(int height, const MeanStats& mean_stats, const Consensus::Params& params)
    {
        LOCK(cs);
        const auto adjusted_height = AdjustedHeight(height, params);

        if(stats.size() <= adjusted_height) {
            return false;
        }

        auto& stat = stats[adjusted_height];
        stat.mean_stats = mean_stats;
        stat.mean_set = true;

        return true;
    }

    bool InviteBuffer::drop(int height, const Consensus::Params& params)
    {
        LOCK(cs);
        const auto adjusted_height = AdjustedHeight(height, params);

        if(stats.size() <= adjusted_height) {
            return false;
        }

        stats.resize(adjusted_height);
        return true;
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
        assert(adjusted_height >= 0);
        if(stats.size() <= static_cast<size_t>(adjusted_height)) {
            stats.resize(adjusted_height+1);
        }
        stats[adjusted_height] = s;
    }

} // namespace pog
