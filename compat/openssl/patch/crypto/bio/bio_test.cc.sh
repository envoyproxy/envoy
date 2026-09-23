#!/bin/bash
set -euo pipefail
uncomment.sh "$1" \
  --comment-gtest-func BIOTest BIOChain \
  --comment-gtest-func BIOTest BIOFreeReturnValue \
  --comment-gtest-func BIOTest BIOFreeReturnValueChain \
  --comment-gtest-func BIOTest CanonicalizeErrors \
  --comment-gtest-func BIOTest FileIO \
  --comment-gtest-func BIOTest ReadASN1ErrorNegative \
  --comment-gtest-func BIOTest SocketConnect \
  --comment-gtest-func BIOTest WriteExConversion
