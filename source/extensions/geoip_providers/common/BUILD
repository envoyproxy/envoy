load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/geoip:geoip_provider_driver_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
