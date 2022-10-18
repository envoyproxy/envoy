load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "c_types_lib",
    hdrs = ["c_types.h"],
    repository = "@envoy",
    deps = [
        "//library/common/data:utility_lib",
    ],
)

envoy_cc_library(
    name = "platform_bridge_cert_validator_lib",
    srcs = ["platform_bridge_cert_validator.cc"],
    hdrs = [
        "platform_bridge_cert_validator.h",
    ],
    repository = "@envoy",
    deps = [
        ":c_types_lib",
        "@envoy//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = [
        "c_types.h",
        "config.h",
    ],
    repository = "@envoy",
    deps = [
        ":platform_bridge_cert_validator_lib",
        "//library/common/api:external_api_lib",
        "//library/common/data:utility_lib",
        "@envoy//envoy/registry",
    ],
)
