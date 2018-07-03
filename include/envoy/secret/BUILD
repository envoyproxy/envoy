licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_manager_interface",
    hdrs = ["secret_manager.h"],
    protobuf_cc_deps = [
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
    deps = [
        "//include/envoy/ssl:tls_certificate_config_interface",
    ],
)
