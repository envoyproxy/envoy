licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "secret_manager_impl_lib",
    srcs = ["secret_manager_impl.cc"],
    hdrs = ["secret_manager_impl.h"],
    deps = [
        ":secret_provider_impl_lib",
        "//include/envoy/secret:secret_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "secret_provider_impl_lib",
    srcs = ["secret_provider_impl.cc"],
    hdrs = ["secret_provider_impl.h"],
    deps = [
        "//include/envoy/secret:secret_provider_interface",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)
