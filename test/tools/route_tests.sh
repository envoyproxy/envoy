#!/bin/bash

PATH_BIN="../../bazel-bin/test/tools/router_check/router_check_tool"
PATH_CONFIG="config"

# Testing expected matches
eval "$PATH_BIN" "$PATH_CONFIG/TestRoutes_router.json" "$PATH_CONFIG/TestRoutes_expected.json" "--details"
eval "$PATH_BIN" "$PATH_CONFIG/ContentType_router.json" "$PATH_CONFIG/ContentType_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/HeaderMatchedRouting_router.json" "$PATH_CONFIG/HeaderMatchedRouting_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/ClusterHeader_router.json" "$PATH_CONFIG/ClusterHeader_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect2_router.json" "$PATH_CONFIG/Redirect2_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect3_router.json" "$PATH_CONFIG/Redirect3_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Weighted_router.json" "$PATH_CONFIG/Weighted_expected.json"

# Mix and match to test for robustness
eval "$PATH_BIN" "$PATH_CONFIG/TestRoutes_router.json" "$PATH_CONFIG/Weighted_expected.json" "--details"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect3_router.json" "$PATH_CONFIG/ContentType_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/HeaderMatchedRouting_router.json" "$PATH_CONFIG/Redirect2_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/ClusterHeader_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Weighted_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect2_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect_router.json" "$PATH_CONFIG/Redirect3_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/ContentType_router.json" "$PATH_CONFIG/Weighted_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/HeaderMatchedRouting_router.json" "$PATH_CONFIG/ContentType_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect_router.json" "$PATH_CONFIG/Redirect2_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect2_router.json" "$PATH_CONFIG/Redirect3_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Redirect3_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Weighted_router.json" "$PATH_CONFIG/Redirect_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/ClusterHeader_router.json" "$PATH_CONFIG/Weighted_expected.json"
eval "$PATH_BIN" "$PATH_CONFIG/Weighted_router.json" "$PATH_CONFIG/TestRoutes_expected.json" "--details"
