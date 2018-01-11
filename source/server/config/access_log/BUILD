licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "file_access_log_lib",
    srcs = ["file_access_log.cc"],
    hdrs = ["file_access_log.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log.cc"],
    hdrs = ["grpc_access_log.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/access_log:grpc_access_log_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
    ],
)
