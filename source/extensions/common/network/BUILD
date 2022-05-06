load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["connection_balancer_config.cc"],
    hdrs = ["connection_balancer_config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/network:connection_balancer_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)
