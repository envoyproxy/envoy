licenses(["notice"])  # Apache 2

# QUICHE-based QUIC listener implementation

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        "//include/envoy/quic:config_interface",
        "//include/envoy/registry",
        "//source/extensions/quic_listeners:well_known_names",
        "@envoy_api//envoy/config/quic_listener/quiche/v2alpha:quiche_cc",
    ],
)
