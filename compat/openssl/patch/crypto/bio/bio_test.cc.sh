#!/bin/bash
set -euo pipefail
uncomment.sh "$1" \
  --comment-gtest-func BIOTest BIOFreeReturnValue \
  --comment-gtest-func BIOTest BIOFreeReturnValueChain
