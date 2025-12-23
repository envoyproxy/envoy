#!/bin/bash

set -euo pipefail

uncomment.sh "$1" \
  --comment-regex '#include "../fipsmodule/' \
  --comment-regex '#include "../test/abi_test.h"' \