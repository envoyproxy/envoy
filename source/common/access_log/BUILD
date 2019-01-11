licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "access_log_formatter_lib",
    srcs = ["access_log_formatter.cc"],
    hdrs = ["access_log_formatter.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:utility_lib",
        "//source/common/stream_info:utility_lib",
    ],
)

envoy_cc_library(
    name = "access_log_manager_lib",
    srcs = ["access_log_manager_impl.cc"],
    hdrs = ["access_log_manager_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = ["access_log_impl.cc"],
    hdrs = ["access_log_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:access_log_config_interface",
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
        "@envoy_api//envoy/config/filter/accesslog/v2:accesslog_cc",
    ],
)
