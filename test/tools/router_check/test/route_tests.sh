#!/bin/bash

set -e

# Router_check_tool binary path
PATH_BIN="${TEST_SRCDIR}/envoy"/test/tools/router_check/router_check_tool

# Config json path
PATH_CONFIG="${TEST_SRCDIR}/envoy"/test/tools/router_check/test/config

TESTS=("ContentType" "ClusterHeader" "DirectResponse" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "Runtime" "TestRoutes" "Weighted")

# Testing expected matches
for t in "${TESTS[@]}"
do
  TEST_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/${t}.yaml" "-t" "${PATH_CONFIG}/${t}.golden.proto.json" "--details")
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
TEST_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/Weighted.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.yaml" "--details")

# Test the proto text test file support
TEST_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/Weighted.yaml" "-t" "${PATH_CONFIG}/Weighted.golden.proto.pb_text" "--details")

# Bad config file
echo "testing bad config output"
BAD_CONFIG_OUTPUT=$(("${PATH_BIN}" "-c" "${PATH_CONFIG}/Redirect.golden.proto.json" "-t" "${PATH_CONFIG}/TestRoutes.yaml") 2>&1) ||
  echo "${BAD_CONFIG_OUTPUT:-no-output}"
if [[ "${BAD_CONFIG_OUTPUT}" != *"Protobuf message (type envoy.config.route.v3.RouteConfiguration reason INVALID_ARGUMENT:tests: Cannot find field.) has unknown fields"* ]]; then
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

# Missing test results
echo "testing missing tests output test cases"
MISSING_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/TestRoutes.yaml" "-t" "${PATH_CONFIG}/TestRoutes.golden.proto.json" "--details" "--covall" 2>&1) ||
  echo "${MISSING_OUTPUT:-no-output}"
if [[ "${MISSING_OUTPUT}" != *"Missing test for host: www2_staging, route: prefix: \"/\""*"Missing test for host: default, route: prefix: \"/api/application_data\""* ]]; then
  exit 1
fi
