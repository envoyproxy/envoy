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
    name = "dynamic_secret_provider_interface",
    hdrs = ["dynamic_secret_provider.h"],
    deps = [
        "secret_callbacks_interface",
        "//include/envoy/ssl:tls_certificate_config_interface",
    ],
)

envoy_cc_library(
    name = "dynamic_secret_provider_factory_interface",
    hdrs = ["dynamic_secret_provider_factory.h"],
    deps = [
        ":dynamic_secret_provider_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)

envoy_cc_library(
    name = "secret_manager_interface",
    hdrs = ["secret_manager.h"],
    deps = [
        ":dynamic_secret_provider_interface",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)
