licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ratelimit_interface",
    hdrs = ["ratelimit.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/tracing:http_tracer_interface",
    ],
)
