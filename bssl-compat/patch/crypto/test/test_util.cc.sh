#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-impl 'operator<<' \
  --uncomment-func-impl DecodeHex \
  --uncomment-func-impl EncodeHex \
  --uncomment-func-impl ErrorEquals \