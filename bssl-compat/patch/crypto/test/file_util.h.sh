#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-class TemporaryFile \
  --uncomment-class TemporaryDirectory \
  --uncomment-class ScopedFD \
  --uncomment-regex 'using ScopedFILE.*' \
  --uncomment-struct 'FileDeleter' \
  --uncomment-regex 'bool SkipTempFileTests();' \

