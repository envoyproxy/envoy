#!/usr/bin/env bash

export ENVOY_BIN="${TEST_SRCDIR}/envoy/test/integration/hotrestart_small_main"

# shellcheck source=test/integration/test_utility.sh
source "${TEST_SRCDIR}/envoy/test/integration/test_utility.sh"

function expect_fail_with_error() {
  log="${TEST_TMPDIR}/envoy.log"
  rm -f "$log"
  expected_error="$1"
  shift
  echo "${ENVOY_BIN} --use-dynamic-base-id $*" ">&" "$log"
  ${ENVOY_BIN} --use-dynamic-base-id "$@" >& "$log"
  EXIT_CODE=$?
  cat "$log"
  check [ $EXIT_CODE -eq 1 ]
  check grep "$expected_error" "$log"
}

function expect_ok() {
  ${ENVOY_BIN} --use-dynamic-base-id "$@"
  EXIT_CODE=$?
  check [ $EXIT_CODE -eq 0 ]
}


start_test "Launching envoy with a bogus command line flag."
expect_fail_with_error "PARSE ERROR: Argument: --bogus-flag" --bogus-flag

start_test "Launching envoy without --config-path or --config-yaml fails."
expect_fail_with_error \
  "At least one of --config-path or --config-yaml or Options::configProto() should be non-empty"

start_test "Launching envoy with unknown IP address."
expect_fail_with_error "error: unknown IP address version" --local-address-ip-version foo

start_test "Launching envoy with unknown mode."
expect_fail_with_error "error: unknown mode" --mode foo

start_test "Launching envoy with bogus component log level."
expect_fail_with_error "error: component log level not correctly specified" --component-log-level upstream:foo:bar

start_test "Launching envoy with invalid log level."
expect_fail_with_error "error: invalid log level specified" --component-log-level upstream:foo

start_test "Launching envoy with invalid component."
expect_fail_with_error "error: invalid component specified" --component-log-level foo:debug

start_test "Launching envoy with empty config and validate."
expect_ok -c "${TEST_SRCDIR}/envoy/test/config/integration/empty.yaml" --mode validate

start_test "Launching envoy with empty config."
run_in_background_saving_pid "${ENVOY_BIN}" -c "${TEST_SRCDIR}/envoy/test/config/integration/empty.yaml" --use-dynamic-base-id
# binary instrumented with coverage is linked dynamically and takes a long time to start.
sleep 120
kill "${BACKGROUND_PID}"
wait "${BACKGROUND_PID}"
