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
        "//envoy/access_log:access_log_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/http:header_map_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "grpc_access_logger_utils_lib",
    srcs = ["grpc_access_logger_utils.cc"],
    hdrs = ["grpc_access_logger_utils.h"],
    deps = [
        "//envoy/common:optref_lib",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "file_access_log_lib",
    srcs = ["file_access_log_impl.cc"],
    hdrs = ["file_access_log_impl.h"],
    # The file based access logger is core code.
    visibility = ["//visibility:public"],
    deps = [
        ":access_log_base",
    ],
)

envoy_cc_library(
    name = "stream_access_log_common_impl_lib",
    hdrs = ["stream_access_log_common_impl.h"],
    deps = [
        ":access_log_base",
        "//source/common/config:config_provider_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:file_access_log_lib",
    ],
)

envoy_cc_library(
    name = "grpc_access_logger",
    hdrs = ["grpc_access_logger.h"],
    deps = [
        ":grpc_access_logger_utils_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
