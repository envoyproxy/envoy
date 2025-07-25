#!/usr/bin/env bash

# Generates the thrift bindings for example.thrift. Requires that
# apache-thrift's thrift generator is installed and on the path.

DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "${DIR}" || exit 1

thrift --gen py --out ./generated example.thrift
