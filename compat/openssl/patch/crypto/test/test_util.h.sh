#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-struct Bytes \
  --uncomment-regex-range 'inline bool operator==(' '}' \
  --uncomment-regex 'inline bool operator!=(' \
  --uncomment-regex 'std::ostream &operator<<(' \
  --uncomment-regex 'bool DecodeHex(' \
  --uncomment-regex 'std::string EncodeHex(' \
  --uncomment-regex 'testing::AssertionResult ErrorEquals.*' \

