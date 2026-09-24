#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro 'PEM_STRING_[[:alnum:]_]*' \
  --uncomment-typedef pem_password_cb \
  --uncomment-func-decl PEM_bytes_read_bio \
  --uncomment-func-decl PEM_X509_INFO_read_bio \
  --uncomment-func-decl PEM_read_bio_PrivateKey \
  --uncomment-func-decl PEM_read_bio_PUBKEY \
  --uncomment-func-decl PEM_read_bio_RSAPrivateKey \
  --uncomment-func-decl PEM_read_bio_X509 \
  --uncomment-func-decl PEM_read_bio_X509_AUX \
  --uncomment-func-decl PEM_read_bio_X509_CRL \
  --uncomment-func-decl PEM_write_bio_X509 \
  --uncomment-macro-redef 'PEM_R_[[:alnum:]_]*'
