#!/bin/bash
# Copyright (c) 2011-2017 The Merit Foundation developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cppcheck --enable=all --inconclusive --xml-version=2 --force --library=windows,posix,gnu ./src 2> result.xml &&\
  cppcheck-htmlreport --title="Merit Core" --source-dir=./ --report-dir=../cppcheck-report --file=result.xml && rm result.xml
