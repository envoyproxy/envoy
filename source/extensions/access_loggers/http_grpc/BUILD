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
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:async_client_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/config/accesslog/v2:als_cc",
        "@envoy_api//envoy/config/filter/accesslog/v2:accesslog_cc",
        "@envoy_api//envoy/service/accesslog/v2:als_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/access_loggers/http_grpc:grpc_access_log_lib",
    ],
)
