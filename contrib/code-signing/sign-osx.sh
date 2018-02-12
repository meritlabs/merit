#!/bin/sh
# Copyright (c) 2018 The Merit Foundation developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

set -euo pipefail

ROOTDIR=dist
BUNDLE="${ROOTDIR}/Merit-Qt.app"
PACKAGE="Merit-Qt.dmg"
CODESIGN=codesign

IDENTITY="$1"

echo "Signing installer image with identity ${IDENTITY}\n"
${CODESIGN} -s ${IDENTITY} ${PACKAGE}
