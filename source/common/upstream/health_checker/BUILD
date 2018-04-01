licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "health_checker_base",
    srcs = ["health_checker_base.cc"],
    hdrs = ["health_checker_base.h"],
    deps = [
        "//include/envoy/upstream:health_checker_interface",
        "//source/common/router:router_lib",
        "@envoy_api//envoy/api/v2/core:health_check_cc",
    ],
)

envoy_cc_library(
    name = "health_checker",
    srcs = ["health_checker_impl.cc"],
    hdrs = ["health_checker_impl.h"],
    external_deps = [
        "grpc_health_proto",
    ],
    deps = [
        ":health_checker_base",
        "//source/common/grpc:codec_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/upstream:host_utility_lib",
        # TODO(mattklein123): The following should not be required once we allow pluggable health
        # checkers.
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
    ],
)
