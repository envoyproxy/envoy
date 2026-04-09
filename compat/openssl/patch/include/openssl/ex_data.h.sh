#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-typedef-redef CRYPTO_EX_DATA \
  --uncomment-typedef CRYPTO_EX_free \
  --uncomment-typedef CRYPTO_EX_dup \
  --uncomment-typedef CRYPTO_EX_unused
