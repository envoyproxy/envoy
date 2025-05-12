load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Example gRPC Credentials

envoy_extension_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["grpc"],
    # Legacy test use.
    visibility = [
        "//test/common/grpc:__subpackages__",
        "//test/extensions:__subpackages__",
    ],
    deps = [
        "//envoy/grpc:google_grpc_creds_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/grpc:google_grpc_creds_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)
