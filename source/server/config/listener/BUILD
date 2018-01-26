licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "original_dst_lib",
    srcs = ["original_dst.cc"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/filter/listener:original_dst_lib",
    ],
)

envoy_cc_library(
    name = "proxy_protocol_lib",
    srcs = ["proxy_protocol.cc"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/filter/listener:proxy_protocol_lib",
    ],
)
