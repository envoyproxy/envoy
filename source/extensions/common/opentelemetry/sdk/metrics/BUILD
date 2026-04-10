load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# OpenTelemetry SDK metrics signal constants.
# Mirrors opentelemetry::sdk::metrics from opentelemetry-cpp.
envoy_extension_package()

envoy_cc_library(
    name = "constants_lib",
    hdrs = ["constants.h"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)
