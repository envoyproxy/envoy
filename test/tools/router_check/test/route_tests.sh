#!/bin/bash

set -e

# Router_check_tool binary path
PATH_BIN="${TEST_SRCDIR}/envoy"/test/tools/router_check/router_check_tool

# Config json path
PATH_CONFIG="${TEST_SRCDIR}/envoy"/test/tools/router_check/test/config

TESTS=("ContentType" "ClusterHeader" "DirectResponse" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "Redirect4" "Runtime" "TestRoutes" "Weighted")

# Testing expected matches
for t in "${TESTS[@]}"
do
  "${PATH_BIN}" "-c" "${PATH_CONFIG}/${t}.yaml" "-t" "${PATH_CONFIG}/${t}.golden.proto.json" "--details"
done

# Testing coverage flag passes
COVERAGE_CMD="${PATH_BIN} -c ${PATH_CONFIG}/Redirect.yaml -t ${PATH_CONFIG}/Redirect.golden.proto.json --details -f "
COVERAGE_OUTPUT=$($COVERAGE_CMD "1.0" 2>&1) || echo "${COVERAGE_OUTPUT:-no-output}"
if [[ "${COVERAGE_OUTPUT}" != *"Current route coverage: "* ]] ; then
  exit 1
fi

COMP_COVERAGE_CMD="${PATH_BIN} -c ${PATH_CONFIG}/ComprehensiveRoutes.yaml -t ${PATH_CONFIG}/ComprehensiveRoutes.golden.proto.json --details -f "
COVERAGE_OUTPUT=$($COMP_COVERAGE_CMD "100" "--covall" 2>&1) || echo "${COVERAGE_OUTPUT:-no-output}"
if [[ "${COVERAGE_OUTPUT}" != *"Current route coverage: 100%"* ]] ; then
  exit 1
fi

DIRECT_RESPONSE_COVERAGE_CMD="${PATH_BIN} -c ${PATH_CONFIG}/DirectResponse.yaml -t ${PATH_CONFIG}/DirectResponse.golden.proto.json --details -f "
COVERAGE_OUTPUT=$($DIRECT_RESPONSE_COVERAGE_CMD "100" "--covall" 2>&1) || echo "${DIRECT_RESPONSE_COVERAGE_CMD:-no-output}"
if [[ "${COVERAGE_OUTPUT}" != *"Current route coverage: 100%"* ]] ; then
  exit 1
fi

RUNTIME_COVERAGE_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/Runtime.yaml" "-t" "${PATH_CONFIG}/Runtime.golden.proto.json" "--details" "--covall" 2>&1) ||
  echo "${RUNTIME_COVERAGE_OUTPUT:-no-output}"
if [[ "${RUNTIME_COVERAGE_OUTPUT}" != *"Current route coverage: 100%"* ]] ; then
  exit 1
fi

# Testing coverage flag fails
COVERAGE_OUTPUT=$($COVERAGE_CMD "100" 2>&1) || echo "${COVERAGE_OUTPUT:-no-output}"
if [[ "${COVERAGE_OUTPUT}" != *"Failed to meet coverage requirement: 100%"* ]] ; then
  exit 1
fi

# Test the yaml test file support
"${PATH_BIN}" "-c" "${PATH_CONFIG}/Weighted.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.yaml" "--details"

# Test the proto text test file support
"${PATH_BIN}" "-c" "${PATH_CONFIG}/Weighted.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.pb_text" "--details"

# Bad config file
echo "testing bad config output"
BAD_CONFIG_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/Redirect.golden.proto.json" "-t" "${PATH_CONFIG}/TestRoutes.yaml" 2>&1) ||
  echo "${BAD_CONFIG_OUTPUT:-no-output}"
if ! [[ "${BAD_CONFIG_OUTPUT}" =~ .*INVALID_ARGUMENT.*envoy.config.route.v3.RouteConfiguration.*tests.* ]]; then
  exit 1
fi

# Failure output flag test cases
echo "testing failure test cases"
# Failure test case with only details flag set
FAILURE_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.json" "--details" 2>&1) ||
  echo "${FAILURE_OUTPUT:-no-output}"
if [[ "${FAILURE_OUTPUT}" != *"Test_1"*"Test_2"*"expected: [cluster1], actual: [instant-server], test type: cluster_name"*"expected: [test_virtual_cluster], actual: [other], test type: virtual_cluster_name"*"Test_3"* ]]; then
  exit 1
fi

# Failure test case with details flag set and failures flag set
FAILURE_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.json" "--details"  "--only-show-failures" 2>&1) ||
  echo "${FAILURE_OUTPUT:-no-output}"
if [[ "${FAILURE_OUTPUT}" != *"Test_2"*"expected: [cluster1], actual: [instant-server], test type: cluster_name"* ]] || [[ "${FAILURE_OUTPUT}" == *"Test_1"* ]]; then
  exit 1
fi

# Failure test case with details flag unset and failures flag set
FAILURE_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.json" "--only-show-failures" 2>&1) ||
  echo "${FAILURE_OUTPUT:-no-output}"
if [[ "${FAILURE_OUTPUT}" != *"Test_2"*"expected: [cluster1], actual: [instant-server], test type: cluster_name"* ]] || [[ "${FAILURE_OUTPUT}" == *"Test_1"* ]]; then
  exit 1
fi

# Failure test case to examine error strings
echo "testing error strings"
FAILURE_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/TestRoutesFailures.golden.proto.json" "--only-show-failures" 2>&1) ||
  echo "${FAILURE_OUTPUT:-no-output}"
if ! echo "${FAILURE_OUTPUT}" | grep -Fxq "expected: [content-type: text/plain], actual: NOT [content-type: text/plain], test type: response_header_matches.string_match.exact"; then
  exit 1
fi
if ! echo "${FAILURE_OUTPUT}" | grep -Fxq "actual: [content-length: 25], test type: response_header_matches.range_match"; then
  exit 1
fi
if ! echo "${FAILURE_OUTPUT}" | grep -Fxq "expected: [x-ping-response: pong], actual: [x-ping-response: yes], test type: response_header_matches.string_match.exact"; then
  exit 1
fi
if ! echo "${FAILURE_OUTPUT}" | grep -Fxq "expected: [has(x-ping-response):false], actual: [has(x-ping-response):true], test type: response_header_matches.present_match"; then
  exit 1
fi
if ! echo "${FAILURE_OUTPUT}" | grep -Fxq "expected: [has(x-pong-response):true], actual: [has(x-pong-response):false], test type: response_header_matches.present_match"; then
  exit 1
fi

# Missing test results
echo "testing missing tests output test cases"
MISSING_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/TestRoutes.golden.proto.json" "--details" "--covall" 2>&1) ||
  echo "${MISSING_OUTPUT:-no-output}"
if [[ "${MISSING_OUTPUT}" != *"Missing test for host: www2_staging, route: prefix: \"/\""*"Missing test for host: default, route: prefix: \"/api/application_data\""* ]]; then
  exit 1
fi

# Detaied coverage flag
echo "test report should contain missing tests with detailed coverage flag"
MISSING_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/DetailedCoverage.yaml" "-t" "${PATH_CONFIG}/DetailedCoverage.golden.proto.json" "--detailed-coverage" 2>&1) ||
  echo "${MISSING_OUTPUT:-no-output}"
if ! echo "${MISSING_OUTPUT}" | grep -qE "Missing test for host: localhost, route name: new_endpoint2-.+"; then
  exit 1
fi

if ! echo "${MISSING_OUTPUT}" | grep -qE "Missing test for host: localhost, route name: -.+"; then
  exit 1
fi

echo "test report shoud not contain missing tests without detailed coverage flag"
MISSING_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/DetailedCoverage.yaml" "-t" "${PATH_CONFIG}/DetailedCoverage.golden.proto.json" 2>&1) ||
  echo "${MISSING_OUTPUT:-no-output}"
if echo "${MISSING_OUTPUT}" | grep -qE "Missing test for host:.+"; then
  exit 1
fi

#  Correctness of route coverage for weighted clusters
echo "route coverage should be calculated correctly for weighted clusters"
MISSING_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/Weighted.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.json" "--details" 2>&1) ||
  echo "${MISSING_OUTPUT:-no-output}"
if ! echo "${MISSING_OUTPUT}" | grep -Fxq "Current route coverage: 100%"; then
  echo "${MISSING_OUTPUT:-no-output}"
  exit 1
fi
