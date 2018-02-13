#!/bin/bash
# Copyright (c) 2018 The Merit Foundation developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

set -eu

function help {
    echo "usage: $0 <tag> <pkcs12> <password>"
    echo "example: $0 0.3.3"

    exit 1
}

TAG=${1:-}
CERTFILE=${2:-}
PASS=${3:-}

if [[ -z "$TAG" ]]; then
    help
fi

if [[ -z "$CERTFILE" ]]; then
    help
fi

if [[ -z "$PASS" ]]; then
    help
fi

BUNDLE="merit-${TAG}-win64-setup.exe"
BUNDLE_SIGNED="merit-${TAG}-win64-setup-signed.exe"
CODESIGN=osslsigncode
TIMESERVER=http://timestamp.comodoca.com

echo "Signing application code\n"
${CODESIGN} sign -pkcs12 "${CERTFILE}" -pass "${PASS}" -t "${TIMESERVER}" -in "${BUNDLE}" -out "${BUNDLE_SIGNED}"

echo "Verifying signatured"
${CODESIGN} verify -in ${BUNDLE_SIGNED}
