#!/bin/bash

# Used in a genrule to wrap sh_test script for execution in
# //test/coverage:coverage_tests single binary.

# Do not generate test suites for empty source files.
if [ -z "$1" ]; then
  exit 0
fi

RAW_TEST_NAME="$(basename "$1")"
# Normalize to something we can use in a TEST(ShTest, ...) name
TEST_NAME="${RAW_TEST_NAME//./_}"

EXEC_ARGS="\"$1\""
shift
for a in $@
do
  EXEC_ARGS="${EXEC_ARGS}, \"$a\""
done

(
  cat << EOF
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

TEST(ShTest, ${TEST_NAME}) {
  Envoy::TestEnvironment::exec({${EXEC_ARGS}});
}
EOF
)
