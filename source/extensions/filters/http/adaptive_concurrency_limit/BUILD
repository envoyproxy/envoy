licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "adaptive_concurrency_limit_lib",
    srcs = ["adaptive_concurrency_limit_impl.cc"],
    hdrs = ["adaptive_concurrency_limit_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:adaptive_concurrency_limit_interface",
    ],
)
