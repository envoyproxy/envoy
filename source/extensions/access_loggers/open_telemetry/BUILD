load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

envoy_extension_package()

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log_impl.cc"],
    hdrs = ["grpc_access_log_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:grpc_access_logger",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)
