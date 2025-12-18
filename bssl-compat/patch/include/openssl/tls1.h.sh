#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'TLSEXT_TYPE_[[:alnum:]_]*' \
  --sed 's/ossl_TLSEXT_TYPE_psk_key_exchange_modes/ossl_TLSEXT_TYPE_psk_kex_modes/' \
  --uncomment-macro TLS1_CK_PSK_WITH_AES_128_CBC_SHA \
  --uncomment-macro TLS1_CK_PSK_WITH_AES_256_CBC_SHA \
  --uncomment-macro TLS1_CK_ECDHE_PSK_WITH_AES_128_CBC_SHA \
  --uncomment-macro TLS1_CK_RSA_WITH_AES_128_SHA \
  --uncomment-macro TLS1_CK_RSA_WITH_AES_256_SHA \
  --uncomment-macro TLS1_CK_RSA_WITH_AES_128_GCM_SHA256 \
  --uncomment-macro TLS1_CK_RSA_WITH_AES_256_GCM_SHA384 \
  --uncomment-macro TLS1_CK_ECDHE_ECDSA_WITH_AES_128_CBC_SHA \
  --uncomment-macro TLS1_CK_ECDHE_ECDSA_WITH_AES_256_CBC_SHA \
  --uncomment-macro TLS1_CK_ECDHE_RSA_WITH_AES_128_CBC_SHA \
  --uncomment-macro TLS1_CK_ECDHE_RSA_WITH_AES_256_CBC_SHA \
  --uncomment-macro TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 \
  --uncomment-macro TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 \
  --uncomment-macro TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256 \
  --uncomment-macro TLS1_CK_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 \
  --uncomment-macro TLS1_CK_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 \
  --uncomment-macro TLS1_3_CK_AES_128_GCM_SHA256 \
  --uncomment-macro TLS1_3_CK_AES_256_GCM_SHA384 \
  --uncomment-macro TLS1_3_CK_CHACHA20_POLY1305_SHA256
