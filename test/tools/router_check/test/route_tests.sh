#!/bin/bash

set -e

# Router_check_tool binary path
PATH_BIN="${TEST_RUNDIR}"/test/tools/router_check/router_check_tool

# Config json path
PATH_CONFIG="${TEST_RUNDIR}"/test/tools/router_check/test/config

TESTS=("ContentType" "ClusterHeader" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "TestRoutes" "Weighted")

# Testing expected matches
for t in "${TESTS[@]}"
do
  TEST_OUTPUT=$("${PATH_BIN}" "${PATH_CONFIG}/${t}.json" "${PATH_CONFIG}/${t}.golden.json" "--details")
done

# Bad config file
BAD_CONFIG_OUTPUT=$(("${PATH_BIN}" "${PATH_CONFIG}/Redirect.golden.json" "${PATH_CONFIG}/TestRoutes.json") 2>&1) ||
  if [[ "${BAD_CONFIG_OUTPUT}" == *"does not conform to schema."* ]]; then
    echo testing bad config output
    echo ${BAD_CONFIG_OUTPUT}
  else
    exit 1
  fi

# Failure test case
FAILURE_OUTPUT=$("${PATH_BIN}" "${PATH_CONFIG}/TestRoutes.json" "${PATH_CONFIG}/Weighted.golden.json" "--details") ||
  if [[ "${FAILURE_OUTPUT}" == *"cluster1 instant-server cluster_name"* ]]; then
    echo testing failure test case
    echo ${FAILURE_OUTPUT}
  else
    exit 1
  fi
