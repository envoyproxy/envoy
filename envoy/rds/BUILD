load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "rds_interface",
    hdrs = [
        "config.h",
        "config_traits.h",
        "route_config_provider.h",
        "route_config_update_receiver.h",
    ],
)
