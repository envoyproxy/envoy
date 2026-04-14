#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'HKDF_R_[a-zA-Z0-9_]*'
