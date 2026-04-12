#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --sed '/openssl\/x509\.h/a#include <ossl/openssl/x509v3.h>' \
  --uncomment-macro-redef 'KU_[[:alnum:]_]*'
  # --uncomment-macro-redef 'X509V3_R_[[:alnum:]_]*' \

