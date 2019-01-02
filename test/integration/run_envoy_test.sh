#!/bin/bash

source "$TEST_RUNDIR/test/integration/test_utility.sh"

function expect_fail_with_error() {
  log="${TEST_TMPDIR}/envoy.log"
  rm -f "$log"
  expected_error="$1"
  shift
  echo ${ENVOY_BIN} "$@" ">&" "$log"
  ${ENVOY_BIN} "$@" >& "$log"
  EXIT_CODE=$?
  cat "$log"
  check [ $EXIT_CODE -eq 1 ]
  check grep "$expected_error" "$log"
}


start_test Launching envoy with a bogus command line flag.
expect_fail_with_error "PARSE ERROR: Argument: --bogus-flag" --bogus-flag

start_test Launching envoy without --config-path or --config-yaml fails.
expect_fail_with_error \
  "At least one of --config-path and --config-yaml should be non-empty"
