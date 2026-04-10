#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro 'SHA[0-9_]*_DIGEST_LENGTH' \
  --uncomment-func-decl SHA1
