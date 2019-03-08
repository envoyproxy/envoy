licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

# Placeholder library to verify/illustrate depending on a QUICHE build target.
# TODO(mpwarres): remove once real build rules added here.
envoy_cc_library(
    name = "dummy_lib",
    srcs = ["dummy.cc"],
    hdrs = ["dummy.h"],
    external_deps = ["quiche_http2_platform"],
)
