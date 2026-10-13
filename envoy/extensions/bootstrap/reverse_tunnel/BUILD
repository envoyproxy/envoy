load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "reverse_tunnel_reporter_lib",
    hdrs = ["reverse_tunnel_reporter.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)
