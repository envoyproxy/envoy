#!/bin/bash

set -e

ORIGINAL_ABI=$1
SDK_ABI=$2

diff "$ORIGINAL_ABI" "$SDK_ABI" || (echo "abi.h in the SDK must be updated" && exit 1)
