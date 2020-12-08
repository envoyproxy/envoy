load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Base class for implementations of AccessLog::Instance.

envoy_extension_package()

envoy_cc_library(
    name = "access_log_base",
    srcs = ["access_log_base.cc"],
    hdrs = ["access_log_base.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/http:header_map_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "grpc_access_logger",
    hdrs = ["grpc_access_logger.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
