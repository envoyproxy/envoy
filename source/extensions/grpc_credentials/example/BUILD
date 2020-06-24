load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Example gRPC Credentials

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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
