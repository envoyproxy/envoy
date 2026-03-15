#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'TRUST_TOKEN_R_[a-zA-Z0-9_]*'
