load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "secret_callbacks_interface",
    hdrs = ["secret_callbacks.h"],
    deps = [
        "//envoy/common:pure_lib",
    ],
)

envoy_cc_library(
    name = "secret_provider_interface",
    hdrs = ["secret_provider.h"],
    deps = [
        ":secret_callbacks_interface",
        "//envoy/common:callback",
        "//envoy/ssl:certificate_validation_context_config_interface",
        "//envoy/ssl:tls_certificate_config_interface",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "secret_manager_interface",
    hdrs = ["secret_manager.h"],
    deps = [
        ":secret_provider_interface",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/init:manager_interface",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
