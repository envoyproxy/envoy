load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "decompressor_factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//envoy/compression/decompressor:decompressor_config_interface",
    ],
)
