#!/bin/bash

# Shell implementation for the fuzzing regression test.

if [ $# -gt 0 ]; then
  readonly LAUNCHER_PATH="$1"; shift
  exec "$LAUNCHER_PATH" --regression "$@"
fi
