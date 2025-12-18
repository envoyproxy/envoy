#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --comment-regex '#include\s*"./internal.h"' \
  --uncomment-regex-range '#if defined(OPENSSL_WINDOWS)' '#endif' \
  --uncomment-gtest-func ErrTest Overflow \
  --uncomment-gtest-func ErrTest ClearError \
  --uncomment-gtest-func ErrTest PreservesErrno \
  --uncomment-gtest-func ErrTest UnknownError \