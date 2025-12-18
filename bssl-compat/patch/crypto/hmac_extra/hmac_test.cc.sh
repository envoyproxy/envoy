#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --comment-regex '#include\s*"\.\./test/wycheproof_util\.h"' \
  --uncomment-func-impl GetDigest \
  --uncomment-gtest-func HMACTest TestVectors