#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl 'CRYPTO_BUFFER_new' \
  --uncomment-func-decl 'CRYPTO_BUFFER_free' \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(CRYPTO_BUFFER,' \
  --uncomment-regex 'DEFINE_STACK_OF(CRYPTO_BUFFER)'
