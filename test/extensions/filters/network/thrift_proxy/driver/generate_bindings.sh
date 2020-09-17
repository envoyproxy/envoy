#!/bin/bash

# Generates the thrift bindings for example.thrift. Requires that
# apache-thrift's thrift generator is installed and on the path.

DIR=$(cd "$(dirname "$0")" && pwd)
cd "${DIR}" || exit 1

thrift --gen py --out ./generated example.thrift
