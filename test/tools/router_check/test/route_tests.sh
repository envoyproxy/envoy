#!/bin/bash

set -e

# router_check_tool binary path
PATH_BIN=$1

# config json path
if [ -z $2 ]
then
  PATH_CONFIG="test/tools/router_check/test/config"
else
  PATH_CONFIG=$2
fi

TESTS=("ContentType" "ClusterHeader" "HeaderMatchedRouting" "Redirect" "Redirect2" "Redirect3" "TestRoutes" "Weighted")
APPEND=("_router" "_expected")

# Testing expected matches
for t in "${TESTS[@]}"
do
  "$PATH_BIN" "$PATH_CONFIG/$t${APPEND[0]}.json" "$PATH_CONFIG/$t${APPEND[1]}.json"
  TEST_OUTPUT=$("$PATH_BIN" "$PATH_CONFIG/$t${APPEND[0]}.json" "$PATH_CONFIG/$t${APPEND[1]}.json" "--details")
done
