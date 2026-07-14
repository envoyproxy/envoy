#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro MD5_DIGEST_LENGTH \
  --uncomment-func-decl MD5
