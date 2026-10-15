#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro ED25519_PRIVATE_KEY_LEN \
  --uncomment-macro ED25519_PUBLIC_KEY_LEN \
  --uncomment-macro ED25519_SIGNATURE_LEN \
  --uncomment-func-decl ED25519_verify \
