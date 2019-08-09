licenses(["notice"])  # Apache 2

# Access log implementation that writes to a gRPC service.
# Public docs: TODO(rodaine): Docs needed.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log_impl.cc"],
    hdrs = ["grpc_access_log_impl.h"],
    deps = [
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/grpc:async_client_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "@envoy_api//envoy/config/accesslog/v2:als_cc",
        "@envoy_api//envoy/config/filter/accesslog/v2:accesslog_cc",
        "@envoy_api//envoy/service/accesslog/v2:als_cc",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_utils",
    srcs = ["grpc_access_log_utils.cc"],
    hdrs = ["grpc_access_log_utils.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:utility_lib",
        "@envoy_api//envoy/data/accesslog/v2:accesslog_cc",
    ],
)

envoy_cc_library(
    name = "http_grpc_access_log_lib",
    srcs = ["http_grpc_access_log_impl.cc"],
    hdrs = ["http_grpc_access_log_impl.h"],
    deps = [
        ":grpc_access_log_lib",
        ":grpc_access_log_utils",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_proto_descriptors_lib",
    srcs = ["grpc_access_log_proto_descriptors.cc"],
    hdrs = ["grpc_access_log_proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/accesslog/v2:als_cc",
    ],
)

envoy_cc_library(
    name = "http_config",
    srcs = ["http_config.cc"],
    hdrs = ["http_config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/access_loggers/grpc:grpc_access_log_proto_descriptors_lib",
        "//source/extensions/access_loggers/grpc:http_grpc_access_log_lib",
    ],
)
