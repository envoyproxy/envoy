licenses(["notice"])  # Apache 2

# Example gRPC Credentials

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["grpc"],
    deps = [
        "//include/envoy/grpc:google_grpc_creds_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/grpc:google_grpc_creds_lib",
        "//source/extensions/grpc_credentials:well_known_names",
    ],
)
