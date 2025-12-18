#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl EVP_PKEY_new \
  --uncomment-func-decl EVP_PKEY_free \
  --uncomment-func-decl EVP_PKEY_up_ref \
  --uncomment-func-decl EVP_PKEY_cmp \
  --uncomment-func-decl EVP_PKEY_id \
  --uncomment-func-decl EVP_PKEY_assign_RSA \
  --uncomment-func-decl EVP_PKEY_get0_RSA \
  --uncomment-func-decl EVP_PKEY_get1_RSA \
  --uncomment-func-decl EVP_PKEY_assign_EC_KEY \
  --uncomment-func-decl EVP_PKEY_get0_EC_KEY \
  --uncomment-func-decl EVP_PKEY_get1_EC_KEY \
  --uncomment-macro-redef 'EVP_PKEY_[A-Z0-9_]*' \
  --uncomment-func-decl EVP_parse_public_key \
  --uncomment-func-decl EVP_parse_private_key \
  --uncomment-func-decl EVP_DigestVerifyInit \
  --uncomment-func-decl EVP_DigestVerify \
  --uncomment-func-decl EVP_PKEY_set1_RSA \
  --uncomment-func-decl EVP_PKEY_get_raw_public_key \
  --uncomment-func-decl EVP_DigestSign \
  --uncomment-func-decl  EVP_DigestSignInit \
  --uncomment-func-decl  EVP_DigestSignUpdate \
  --uncomment-func-decl  EVP_DigestSignFinal \
  --uncomment-func-decl EVP_DigestVerifyUpdate \
  --uncomment-func-decl EVP_DigestVerifyFinal \
  --uncomment-func-decl EVP_PKEY_CTX_set_rsa_padding \
  --uncomment-func-decl EVP_PKEY_CTX_set_rsa_mgf1_md \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(EVP_PKEY,' \
  --uncomment-regex 'BORINGSSL_MAKE_UP_REF(EVP_PKEY,' \
  --uncomment-func-decl EVP_PKEY_size \
  --uncomment-func-decl EVP_PKEY_bits \
  --uncomment-func-decl EVP_SignInit_ex \
  --uncomment-func-decl EVP_SignUpdate \
  --uncomment-func-decl EVP_SignFinal

