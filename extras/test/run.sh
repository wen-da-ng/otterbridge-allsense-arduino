#!/usr/bin/env bash
# Builds and runs the host tests (needs g++). From the library folder: bash extras/test/run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -Werror -I shim -I ../../src -o /tmp/allsense_compact_test test_compact.cpp ../../src/AllSenseSerial.cpp
/tmp/allsense_compact_test
