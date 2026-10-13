#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-impl SkipTempFileTests \
  --uncomment-func-impl GetTempDir \
  --uncomment-func-impl TemporaryFile::Init \
  --uncomment-func-impl TemporaryFile::Open \
  --uncomment-func-impl TemporaryFile::OpenFD \
  --uncomment-regex-range  'TemporaryFile::~TemporaryFile.*' '}'
