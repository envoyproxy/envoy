#!/bin/bash

set -euo pipefail

uncomment.sh "$1" \
  --comment-regex '^static const MD sha512_256' \
  --comment-regex '^static const MD blake2b256' \
  --comment-regex-range '^\s*// SHA-512-256 tests' '^$' \
  --comment-regex-range '^\s*// BLAKE2b-256 tests' '},\s*$' \
  --comment-gtest-func DigestTest Getters \
  --comment-gtest-func DigestTest ASN1 \
  --comment-gtest-func DigestTest TransformBlocks \
