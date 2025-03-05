load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "algorithm_provider_interface",
    hdrs = ["algorithm_provider.h"],
)

envoy_cc_library(
    name = "algorithm_provider_config_interface",
    hdrs = ["factory.h"],
    deps = [
        ":algorithm_provider_interface",
        "//envoy/config:typed_config_interface",
    ],
)
