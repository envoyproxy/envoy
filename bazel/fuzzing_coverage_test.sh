#!/bin/bash

# Shell implementation for the fuzzing coverage test.

if [ $# -gt 0 ]; then
  readonly BINARY_PATH="$1"; shift
  exec "$BINARY_PATH" "$@"
fi
