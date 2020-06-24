load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "instance_interface",
    hdrs = ["instance.h"],
)

envoy_cc_library(
    name = "manager_interface",
    hdrs = ["manager.h"],
    deps = [
        ":instance_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/registry",
    ],
)
