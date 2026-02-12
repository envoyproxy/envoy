#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl EVP_DecodedLength \
  --uncomment-func-decl EVP_DecodeBase64
