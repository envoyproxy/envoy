#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include\s*<openssl/' \
  --uncomment-func-impl OPENSSL_isdigit \
  --uncomment-func-impl OPENSSL_fromxdigit \
  --uncomment-func-impl OPENSSL_isspace \
