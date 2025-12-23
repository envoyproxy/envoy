#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro 'PEM_STRING_[[:alnum:]_]*' \
  --uncomment-macro DECLARE_PEM_read_fp \
  --uncomment-macro DECLARE_PEM_write_fp \
  --uncomment-macro DECLARE_PEM_write_cb_fp \
  --uncomment-macro DECLARE_PEM_read_bio \
  --uncomment-macro DECLARE_PEM_write_bio \
  --uncomment-macro DECLARE_PEM_write_cb_bio \
  --uncomment-macro DECLARE_PEM_write \
  --uncomment-macro DECLARE_PEM_write_cb \
  --uncomment-macro DECLARE_PEM_read \
  --uncomment-macro DECLARE_PEM_rw \
  --uncomment-macro DECLARE_PEM_rw_cb \
  --uncomment-typedef pem_password_cb \
  --uncomment-func-decl PEM_bytes_read_bio \
  --uncomment-func-decl PEM_X509_INFO_read_bio \
  --uncomment-regex 'DECLARE_PEM_rw(X509,' \
  --uncomment-regex 'DECLARE_PEM_rw(X509_CRL,' \
  --uncomment-regex 'DECLARE_PEM_rw(X509_AUX,' \
  --uncomment-regex 'DECLARE_PEM_rw_cb(RSAPrivateKey,' \
  --uncomment-regex 'DECLARE_PEM_rw_cb(PrivateKey,' \
  --uncomment-regex 'DECLARE_PEM_rw(PUBKEY,' \
  --uncomment-macro-redef 'PEM_R_[[:alnum:]_]*'
