#!/bin/bash

set -e

# Router_check_tool binary path
PATH_BIN=/private/var/tmp/_bazel_jmahapatra/2fb333e5536650745e9f1244a12ec391/execroot/envoy/bazel-out/darwin-dbg/bin/test/tools/router_check/router_check_tool
TEST_RUNDIR="/Users/jmahapatra/src/envoy"
# Config json path
PATH_CONFIG="${TEST_RUNDIR}"/test/tools/router_check/test/config

TESTS=("ContentType" "ClusterHeader" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "TestRoutes" "Weighted")

# Testing expected matches
for t in "${TESTS[@]}"
do
  TEST_OUTPUT=$("${PATH_BIN}" "${PATH_CONFIG}/${t}.yaml" "${PATH_CONFIG}/${t}.golden.json" "--details")
done

# Testing expected matches using --useproto
# --useproto needs the test schema as a validation.proto message.
for t in "${TESTS[@]}"
do
  sed -i '1s/^/{"tests":\n/' "${PATH_CONFIG}/${t}.golden.json"
  sed -i "\$a}" "${PATH_CONFIG}/${t}.golden.json"
  TEST_OUTPUT=$("${PATH_BIN}" "${PATH_CONFIG}/${t}.yaml" "${PATH_CONFIG}/${t}.golden.json" "--details" "--useproto")
  sed -i '$ d' "${PATH_CONFIG}/${t}.golden.json"
  sed -i '1d' "${PATH_CONFIG}/${t}.golden.json"
done

# Bad config file
echo testing bad config output
BAD_CONFIG_OUTPUT=$(("${PATH_BIN}" "${PATH_CONFIG}/Redirect.golden.json" "${PATH_CONFIG}/TestRoutes.yaml") 2>&1) ||
  echo ${BAD_CONFIG_OUTPUT:-no-output}
if [[ "${BAD_CONFIG_OUTPUT}" != *"Unable to parse"* ]]; then
  exit 1
fi

# Failure test case
echo testing failure test case
FAILURE_OUTPUT=$("${PATH_BIN}" "${PATH_CONFIG}/TestRoutes.yaml" "${PATH_CONFIG}/Weighted.golden.json" "--details" 2>&1) ||
  echo ${FAILURE_OUTPUT:-no-output}
if [[ "${FAILURE_OUTPUT}" != *"expected: [cluster1], actual: [instant-server], test type: cluster_name"* ]]; then
  exit 1
fi
