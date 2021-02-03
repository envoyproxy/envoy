load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "compressor_interface",
    hdrs = ["compressor.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "compressor_factory_interface",
    hdrs = ["factory.h"],
    deps = [
        ":compressor_interface",
    ],
)

envoy_cc_library(
    name = "compressor_config_interface",
    hdrs = ["config.h"],
    deps = [
        ":compressor_factory_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/server:filter_config_interface",
    ],
)
