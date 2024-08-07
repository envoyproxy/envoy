#!/bin/bash

set -e

ORIGINAL_ABI=$1
GO_ABI=$2
RUST_ABI=$3

diff "$ORIGINAL_ABI" "$GO_ABI" || (echo "abi.h in Go SDK must be updated" && exit 1)
diff "$ORIGINAL_ABI" "$RUST_ABI" || (echo "abi.h in Rust SDK must be updated" && exit 1)
