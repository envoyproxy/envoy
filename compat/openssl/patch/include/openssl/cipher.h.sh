#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl EVP_aes_128_gcm \
  --uncomment-func-decl EVP_aes_256_gcm \
  --uncomment-func-decl EVP_aes_256_cbc \
  --uncomment-func-decl EVP_CIPHER_CTX_new \
  --uncomment-func-decl EVP_CIPHER_CTX_free \
  --uncomment-macro-redef EVP_CTRL_AEAD_GET_TAG \
  --uncomment-macro-redef EVP_CTRL_GCM_SET_IVLEN \
  --uncomment-macro-redef EVP_CTRL_GCM_GET_TAG \
  --uncomment-macro-redef EVP_CTRL_GCM_SET_TAG \
  --uncomment-func-decl EVP_EncryptInit_ex \
  --uncomment-func-decl EVP_DecryptInit_ex \
  --uncomment-func-decl EVP_EncryptUpdate \
  --uncomment-func-decl EVP_EncryptFinal_ex \
  --uncomment-func-decl EVP_DecryptUpdate \
  --uncomment-func-decl EVP_DecryptFinal_ex \
  --uncomment-func-decl EVP_CIPHER_block_size \
  --uncomment-func-decl EVP_CIPHER_key_length \
  --uncomment-func-decl EVP_CIPHER_iv_length \
  --uncomment-func-decl EVP_CIPHER_CTX_ctrl \
  --uncomment-macro-redef 'EVP_MAX_[A-Z0-9_]*_LENGTH' \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(EVP_CIPHER_CTX' \
  --uncomment-macro-redef 'CIPHER_R_[[:alnum:]_]*'
