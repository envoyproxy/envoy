#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro 'SHA[0-9_]*_DIGEST_LENGTH' \
  --uncomment-func-decl SHA1 \
  --uncomment-func-decl SHA224 \
  --uncomment-func-decl SHA256 \
  --uncomment-func-decl SHA384 \
  --uncomment-func-decl SHA512 \
  --uncomment-func-decl SHA512_256 \
  --uncomment-func-decl SHA256_Init \
  --uncomment-func-decl SHA256_Update \
  --uncomment-func-decl SHA256_Final \

