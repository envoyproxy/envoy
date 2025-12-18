#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl OPENSSL_malloc \
  --uncomment-func-decl OPENSSL_free \
  --uncomment-func-decl OPENSSL_realloc \
  --uncomment-func-decl CRYPTO_memcmp \
  --uncomment-func-decl OPENSSL_isdigit \
  --uncomment-func-decl OPENSSL_fromxdigit \
  --uncomment-func-decl OPENSSL_isspace \
  --uncomment-macro DECIMAL_SIZE \
  --uncomment-func-decl BIO_snprintf \
  --uncomment-func-decl OPENSSL_memdup \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(char,' \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(uint8_t,'
