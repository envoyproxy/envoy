#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl PKCS12_get_key_and_certs \
  --uncomment-func-decl d2i_PKCS12_bio \
  --uncomment-func-decl PKCS12_parse \
  --uncomment-func-decl PKCS12_verify_mac \
  --uncomment-func-decl PKCS12_free \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(PKCS12,' \
  --uncomment-macro-redef 'PKCS8_R_[[:alnum:]_]*'
