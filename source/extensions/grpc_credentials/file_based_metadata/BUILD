load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# File Based Metadata gRPC Credentials

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["grpc"],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        "//include/envoy/grpc:google_grpc_creds_interface",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:google_grpc_creds_lib",
        "//source/extensions/grpc_credentials:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/grpc_credential/v3:pkg_cc_proto",
    ],
)
