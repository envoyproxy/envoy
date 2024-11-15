load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# AWS IAM gRPC Credentials

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["grpc"],
    deps = [
        "//envoy/grpc:google_grpc_creds_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:google_grpc_creds_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:region_provider_impl_lib",
        "//source/extensions/common/aws:sigv4_signer_impl_lib",
        "//source/extensions/common/aws:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/grpc_credential/v3:pkg_cc_proto",
    ],
)
