#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#\(ifndef\|define\|endif\).*OPENSSL_HEADER_CRYPTO_INTERNAL_H' \
  --uncomment-regex '#include\s*<openssl/' \
  --comment-regex '#include\s*<openssl/prefix_symbols' \
  --uncomment-func-impl OPENSSL_memcpy \
  --uncomment-func-impl OPENSSL_memmove \
  --uncomment-func-impl OPENSSL_memset \
  --sed 's/^inline \(void \*OPENSSL_mem\)/static inline \1/' \

