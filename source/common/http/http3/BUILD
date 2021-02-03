load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "quic_codec_factory_lib",
    hdrs = ["quic_codec_factory.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = ["//source/common/singleton:const_singleton"],
)
