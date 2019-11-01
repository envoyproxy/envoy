licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
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
        "coverage.cc",
        "coverage.h",
        "router.cc",
        "router.h",
    ],
    copts = ["-DHAVE_LONG_LONG"],
    external_deps = ["tclap"],
    deps = [
        ":validation_proto_cc_proto",
        "//source/common/event:dispatcher_lib",
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

envoy_proto_library(
    name = "validation_proto",
    srcs = ["validation.proto"],
    deps = ["@envoy_api//envoy/api/v2/core:pkg"],
)
