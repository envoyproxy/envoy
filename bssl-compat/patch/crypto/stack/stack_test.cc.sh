#!/bin/bash

set -euo pipefail

uncomment.sh "$1" \
  --comment-gtest-func StackTest DeleteIf \