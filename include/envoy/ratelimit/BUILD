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
        "//include/envoy/registry",
        "//include/envoy/tracing:http_tracer_interface",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
        "@envoy_api//envoy/config/ratelimit/v2:rls_cc",
    ],
)
