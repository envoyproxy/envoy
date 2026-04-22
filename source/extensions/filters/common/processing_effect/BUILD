load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "processing_effect_lib",
    hdrs = ["processing_effect.h"],
    tags = ["skip_on_windows"],
)
