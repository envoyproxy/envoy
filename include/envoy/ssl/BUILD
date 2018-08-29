licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "connection_interface",
    hdrs = ["connection.h"],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
)

envoy_cc_library(
    name = "context_config_interface",
    hdrs = ["context_config.h"],
    deps = [
        ":certificate_validation_context_config_interface",
        ":tls_certificate_config_interface",
    ],
)

envoy_cc_library(
    name = "context_manager_interface",
    hdrs = ["context_manager.h"],
    deps = [
        ":context_config_interface",
        ":context_interface",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "tls_certificate_config_interface",
    hdrs = ["tls_certificate_config.h"],
)

envoy_cc_library(
    name = "certificate_validation_context_config_interface",
    hdrs = ["certificate_validation_context_config.h"],
)
