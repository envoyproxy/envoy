load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "decompressor_config_interface",
    hdrs = ["config.h"],
    deps = [
        ":decompressor_factory_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "decompressor_factory_interface",
    hdrs = ["factory.h"],
    deps = [
        ":decompressor_interface",
    ],
)

envoy_cc_library(
    name = "decompressor_interface",
    hdrs = ["decompressor.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
    ],
)
