#!/bin/sh
# Copyright (c) 2018 The Merit Foundation developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

set -euo pipefail

ROOTDIR=dist
BUNDLE="${ROOTDIR}/Merit-Qt.app"
PACKAGE="Merit-Qt.dmg"
CODESIGN=codesign

IDENTITY=${1:-}
APP_TYPE=${2:-}

function help {
    echo "usage: $0 <identitty> <app-type>"
    echo "    app-type: 1 or 2"
    echo "    1 - sign ${BUNDLE}"
    echo "    2 - sign ${PACKAGE}"
    echo "example: $0 MyIdentity 1"

    exit 1
}

if [[ -z "$IDENTITY" ]]; then
    help
fi

if [[ -z "$APP_TYPE" ]]; then
    help
fi

if [ "$APP_TYPE" == "1" ]; then
    echo "Signing application code with identity ${IDENTITY}\n"
    ${CODESIGN} -s ${IDENTITY} -v --deep ${BUNDLE}
    echo "Verifying signatured"
    ${CODESIGN} -v ${BUNDLE}
fi

if [ "$APP_TYPE" == "2" ]; then
    echo "Signing installer image with identity ${IDENTITY}\n"
    ${CODESIGN} -s ${IDENTITY} -v --deep ${PACKAGE}
    echo "Verifying signatured"
    ${CODESIGN} -v ${PACKAGE}
fi
