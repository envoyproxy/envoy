licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_callbacks_interface",
    hdrs = ["secret_callbacks.h"],
)

envoy_cc_library(
    name = "secret_provider_interface",
    hdrs = ["secret_provider.h"],
    deps = [
        ":secret_callbacks_interface",
        "//include/envoy/common:callback",
        "//include/envoy/ssl:certificate_validation_context_config_interface",
        "//include/envoy/ssl:tls_certificate_config_interface",
    ],
)

envoy_cc_library(
    name = "secret_manager_interface",
    hdrs = ["secret_manager.h"],
    deps = [
        ":secret_provider_interface",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)
