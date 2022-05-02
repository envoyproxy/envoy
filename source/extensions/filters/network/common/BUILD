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
    # This is useful code for registering filters, both for in-repo and out-of-repo
    # extensions. Mark as public because there's no way to allow external visibility
    # but prevent core-Envoy visibility.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/server:filter_config_interface",
    ],
)
