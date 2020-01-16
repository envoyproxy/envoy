licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_id_utils_lib",
    srcs = ["request_id_utils_impl.cc"],
    hdrs = ["request_id_utils_impl.h"],
    deps = [
        "//include/envoy/request_id_utils:request_id_utils_interface",
        "//include/envoy/server:request_id_utils_config_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)
