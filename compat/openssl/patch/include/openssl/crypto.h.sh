#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'OPENSSL_INIT_[[:alnum:]_]*' \
  --uncomment-func-decl FIPS_mode
