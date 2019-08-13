licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tls_certificate_config_impl_lib",
    srcs = ["tls_certificate_config_impl.cc"],
    hdrs = ["tls_certificate_config_impl.h"],
    deps = [
        "//include/envoy/ssl:tls_certificate_config_interface",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "certificate_validation_context_config_impl_lib",
    srcs = ["certificate_validation_context_config_impl.cc"],
    hdrs = ["certificate_validation_context_config_impl.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/ssl:certificate_validation_context_config_interface",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)
