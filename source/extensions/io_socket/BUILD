load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "address_map",
    hdrs = [
        "address_map.h",
    ],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//include/envoy/network:address_interface",
    ],
)

envoy_cc_library(
    name = "client_connection_factory",
    hdrs = ["config.h"],
    deps = [
        ":address_map",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)
