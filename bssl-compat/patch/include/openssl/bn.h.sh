#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --sed '/stdio\.h/a#include <ossl/openssl/bn.h>' \
  --uncomment-macro-redef 'BN_R_[a-zA-Z0-9_]*' \
  --uncomment-typedef BN_ULONG \
  --uncomment-macro BN_BITS2 \
  --uncomment-macro 'BN_\(DEC\|HEX\)_FMT[12]' \
  --uncomment-func-decl BN_new \
  --uncomment-func-decl BN_free \
  --uncomment-func-decl BN_dup \
  --uncomment-func-decl BN_num_bits \
  --uncomment-func-decl BN_set_word \
  --uncomment-func-decl BN_bn2hex \
  --uncomment-func-decl BN_hex2bn \
  --uncomment-func-decl BN_bn2dec \
  --uncomment-func-decl BN_add_word \
  --uncomment-func-decl BN_cmp_word \
  --uncomment-func-decl BN_ucmp \
  --uncomment-func-decl BN_bin2bn \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(BIGNUM' \
