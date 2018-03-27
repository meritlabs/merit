// Copyright (c) 2017-2018 The Merit Foundation developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, nEdgeBits=%d, vtx=%u, invites=%u, refs=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        nEdgeBits,
        vtx.size(),
        invites.size(),
        m_vRef.size());

    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }

    for (const auto& inv : invites) {
        s << "  " << inv->ToString() << "\n";
    }

    for (const auto& ref : m_vRef) {
        s << "  " << ref->ToString() << "\n";
    }

    return s.str();
}
