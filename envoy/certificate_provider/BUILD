load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "certificate_provider_manager_interface",
    hdrs = ["certificate_provider_manager.h"],
    deps = [
        "//source/extensions/certificate_providers:certificate_provider_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
