#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-macro-redef 'DH_R_[a-zA-Z0-9_]*' \
