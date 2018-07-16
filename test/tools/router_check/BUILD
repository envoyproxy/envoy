licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_binary(
    name = "router_check_tool",
    srcs = ["router_check.cc"],
    deps = [":router_check_main_lib"],
)

envoy_cc_test_library(
    name = "router_check_main_lib",
    srcs = [
        "router.cc",
        "router.h",
    ],
    deps = [
        "//source/common/config:rds_json_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/router:config_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
        "//test/tools/router_check/json:tool_config_schemas_lib",
    ],
)
