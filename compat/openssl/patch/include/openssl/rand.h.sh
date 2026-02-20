#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl RAND_bytes \
  --uncomment-func-decl RAND_enable_fork_unsafe_buffering
