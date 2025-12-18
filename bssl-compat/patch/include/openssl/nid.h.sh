#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'SN_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'LN_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'NID_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'OBJ_[a-zA-Z0-9_]*' \
	--sed 's|^// \s*1L, .*$||g' \
	--sed 's|^// \s*"[^"]*"$||g'
