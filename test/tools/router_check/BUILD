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
    deps = [":router_check_main_lib"],
)

envoy_cc_test_library(
    name = "router_check_main_lib",
    srcs = [
        "coverage.cc",
        "coverage.h",
        "router.cc",
        "router.h",
        "router_check.cc",
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
        "//source/exe:platform_impl_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "validation_proto",
    srcs = ["validation.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)
