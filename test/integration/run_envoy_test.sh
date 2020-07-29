#!/bin/bash

export ENVOY_BIN="${TEST_SRCDIR}/envoy/test/integration/hotrestart_main"
source "${TEST_SRCDIR}/envoy/test/integration/test_utility.sh"

function expect_fail_with_error() {
  log="${TEST_TMPDIR}/envoy.log"
  rm -f "$log"
  expected_error="$1"
  shift
  echo ${ENVOY_BIN} --use-dynamic-base-id "$@" ">&" "$log"
  ${ENVOY_BIN} --use-dynamic-base-id "$@" >& "$log"
  EXIT_CODE=$?
  cat "$log"
  check [ $EXIT_CODE -eq 1 ]
  check grep "$expected_error" "$log"
}


start_test Launching envoy with a bogus command line flag.
expect_fail_with_error "PARSE ERROR: Argument: --bogus-flag" --bogus-flag

start_test Launching envoy without --config-path or --config-yaml fails.
expect_fail_with_error \
  "At least one of --config-path or --config-yaml or Options::configProto() should be non-empty"

start_test Launching envoy with unknown IP address.
expect_fail_with_error "error: unknown IP address version" --local-address-ip-version foo

start_test Launching envoy with unknown mode.
expect_fail_with_error "error: unknown mode" --mode foo

start_test Launching envoy with bogus component log level.
expect_fail_with_error "error: component log level not correctly specified" --component-log-level upstream:foo:bar

start_test Launching envoy with invalid log level.
expect_fail_with_error "error: invalid log level specified" --component-log-level upstream:foo

start_test Launching envoy with invalid component.
expect_fail_with_error "error: invalid component specified" --component-log-level foo:debug
