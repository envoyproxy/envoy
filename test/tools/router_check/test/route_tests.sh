#!/bin/bash

set -e

# router_check_tool binary path
PATH_BIN="test/tools/router_check/router_check_tool"

# config json path
PATH_CONFIG="test/tools/router_check/test/config"

TESTS=("ContentType" "ClusterHeader" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "TestRoutes" "Weighted")

# Testing expected matches
for t in "${TESTS[@]}"
do
  "${PATH_BIN}" "${PATH_CONFIG}/${t}.json" "${PATH_CONFIG}/${t}.golden.json"
done
