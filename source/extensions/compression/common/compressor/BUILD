load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "compressor_factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//envoy/compression/compressor:compressor_config_interface",
        "//envoy/compression/compressor:compressor_factory_interface",
        "//envoy/server:filter_config_interface",
    ],
)
