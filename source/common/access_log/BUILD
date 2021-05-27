load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "access_log_lib",
    srcs = ["access_log_impl.cc"],
    hdrs = ["access_log_impl.h"],
    external_deps = [
        "abseil_hash",
    ],
    deps = [
        "//include/envoy/access_log:access_log_interface_no_include_prefix",
        "//include/envoy/config:typed_config_interface_no_include_prefix",
        "//include/envoy/filesystem:filesystem_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
        "//include/envoy/runtime:runtime_interface_no_include_prefix",
        "//include/envoy/server:access_log_config_interface_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_manager_lib",
    srcs = ["access_log_manager_impl.cc"],
    hdrs = ["access_log_manager_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface_no_include_prefix",
        "//include/envoy/api:api_interface_no_include_prefix",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
    ],
)
