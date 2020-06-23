load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "compressor_factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//include/envoy/compression/compressor:compressor_config_interface",
        "//include/envoy/compression/compressor:compressor_factory_interface",
        "//include/envoy/server:filter_config_interface",
    ],
)
