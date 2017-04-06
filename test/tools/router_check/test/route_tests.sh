#!/bin/bash

set -e

# router_check_tool binary path
if [ -z $1 ]
then
  PATH_BIN="test/tools/router_check/router_check_tool"
else
  PATH_BIN=$1
fi

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
