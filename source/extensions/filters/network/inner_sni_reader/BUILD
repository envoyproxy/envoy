licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "inner_sni_reader",
    srcs = ["inner_sni_reader.cc"],
    hdrs = ["inner_sni_reader.h"],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":inner_sni_reader",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
    ],
)
