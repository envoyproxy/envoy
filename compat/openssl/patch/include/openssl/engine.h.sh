#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-macro-redef 'ENGINE_R_[a-zA-Z0-9_]*'
