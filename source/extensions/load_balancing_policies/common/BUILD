load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "factory_base",
    hdrs = ["factory_base.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_factory_base_lib",
    ],
)
