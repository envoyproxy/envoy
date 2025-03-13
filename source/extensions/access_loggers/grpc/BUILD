load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Access log implementation that writes to a gRPC service.
# Public docs: TODO(rodaine): Docs needed.

envoy_extension_package()

envoy_cc_library(
    name = "config_utils",
    srcs = ["config_utils.cc"],
    hdrs = ["config_utils.h"],
    deps = [
        ":grpc_access_log_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/singleton:instance_interface",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log_impl.cc"],
    hdrs = ["grpc_access_log_impl.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/extensions/access_loggers/common:grpc_access_logger",
        "//source/extensions/access_loggers/common:grpc_access_logger_clients_lib",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_utils",
    srcs = ["grpc_access_log_utils.cc"],
    hdrs = ["grpc_access_log_utils.h"],
    deps = [
        "//envoy/upstream:upstream_interface",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:utility_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_grpc_access_log_lib",
    srcs = ["http_grpc_access_log_impl.cc"],
    hdrs = ["http_grpc_access_log_impl.h"],
    deps = [
        ":grpc_access_log_lib",
        ":grpc_access_log_utils",
        "//source/extensions/access_loggers/common:access_log_base",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tcp_grpc_access_log_lib",
    srcs = ["tcp_grpc_access_log_impl.cc"],
    hdrs = ["tcp_grpc_access_log_impl.h"],
    deps = [
        ":grpc_access_log_lib",
        ":grpc_access_log_utils",
        "//source/extensions/access_loggers/common:access_log_base",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_proto_descriptors_lib",
    srcs = ["grpc_access_log_proto_descriptors.cc"],
    hdrs = ["grpc_access_log_proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_extension(
    name = "http_config",
    srcs = ["http_config.cc"],
    hdrs = ["http_config.h"],
    deps = [
        ":config_utils",
        "//envoy/access_log:access_log_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/grpc:grpc_access_log_proto_descriptors_lib",
        "//source/extensions/access_loggers/grpc:http_grpc_access_log_lib",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "tcp_config",
    srcs = ["tcp_config.cc"],
    hdrs = ["tcp_config.h"],
    deps = [
        ":config_utils",
        "//envoy/access_log:access_log_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/grpc:grpc_access_log_proto_descriptors_lib",
        "//source/extensions/access_loggers/grpc:tcp_grpc_access_log_lib",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)
