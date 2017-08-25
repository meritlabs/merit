// Copyright (c) 2017 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ref_verify.h"

#include "primitives/referral.h"
#include "validation.h"

// TODO remove the following dependencies
#include "chain.h"
namespace Consensus 
{
    namespace
    {
        const int TOO_SMALL_SCRIPT = 2;
        const int TOO_BIG_SCRIPT = 100;
    }

    bool CheckReferral(const Referral& ref, CValidationState& state)
    {
        if(ref.previousReferral.IsNull())
            return state.DoS(100, false, REJECT_INVALID, "bad-ref-prev-null");

        if (ref.scriptSig.size() < TOO_SMALL_SCRIPT || ref.scriptSig.size() > TOO_BIG_SCRIPT)
            return state.DoS(100, false, REJECT_INVALID, "bad-ref-cb-length");

        //TODO: More static checks here, for example, making sure the script has
        //signatures.

        return true;
    }
}
