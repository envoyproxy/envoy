#!/bin/bash
set -e

# Bazel's sh_test sets the working directory to the root of the runfiles tree.
# We need to find the node executable.
NODE_EXECUTABLE=$(command -v node)
if [ -z "$NODE_EXECUTABLE" ]; then
  # Try to find it in common locations
  if [ -x "/usr/bin/node" ]; then NODE_EXECUTABLE="/usr/bin/node";
  elif [ -x "/usr/local/bin/node" ]; then NODE_EXECUTABLE="/usr/local/bin/node";
  fi
fi

if [ -z "$NODE_EXECUTABLE" ]; then
  echo "node not found in PATH"
  exit 1
fi

# The first argument is the test script to run.
TEST_SCRIPT="$1"
shift

# Run the provided script with any remaining arguments
$NODE_EXECUTABLE "$TEST_SCRIPT" "$@"
