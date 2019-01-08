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

start_test Launching envoy with unknown IP address.
expect_fail_with_error "error: unknown IP address version"

start_test Launching envoy with unknown mode.
expect_fail_with_error "error: unknown mode"

start_test Launching envoy with bogus component log level.
expect_fail_with_error "error: component log level not correctly specified"

start_test Launching envoy with invalid log level.
expect_fail_with_error "error: invalid log level specified"

start_test Launching envoy with invalid component.
expect_fail_with_error "error: invalid component specified"

start_test Launching envoy with max-obj-name-len value less than minimum value of 60.
expect_fail_with_error "error: the 'max-obj-name-len' value specified .* is less than the minimum"

start_test Launching envoy with max-stats value more than maximum value of 100M.
expect_fail_with_error "error: the 'max-stats' value specified .* is more than the maximum value"
