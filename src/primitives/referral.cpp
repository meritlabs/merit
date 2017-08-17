// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/referral.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "net.h"
#include "util.h"

CMutableReferral::CMutableReferral() : 
    nVersion(CReferral::CURRENT_VERSION),
    previousReferral(),
    scriptSig() {}
CMutableReferral::CMutableReferral(const CReferral& ref) : 
    nVersion(ref.nVersion),
    previousReferral(ref.previousReferral),
    scriptSig(ref.scriptSig) {}

uint256 CMutableReferral::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

uint256 CReferral::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CReferral::CReferral() : 
    nVersion(CReferral::CURRENT_VERSION),
    previousReferral(),
    scriptSig(),
    hash() {}

CReferral::CReferral(const CMutableReferral &ref) : 
    nVersion(ref.nVersion),
    previousReferral(ref.previousReferral),
    scriptSig(ref.scriptSig),
    hash(ComputeHash()) {}

CReferral::CReferral(CMutableReferral &&ref) : 
    nVersion(ref.nVersion),
    previousReferral(std::move(ref.previousReferral)),
    scriptSig(std::move(ref.scriptSig)),
    hash(ComputeHash()) {}

unsigned int CReferral::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string CReferral::ToString() const
{
    std::string str;
    str += strprintf("CReferral(hash=%s, ver=%d, previousReferral=%s, scriptSig=%s)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        HexStr(previousReferral),
        HexStr(scriptSig));
    return str;
}
