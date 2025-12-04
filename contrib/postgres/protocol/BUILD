load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Shared PostgreSQL protocol constants library.
# Used by both postgres_proxy and postgres_inspector filters.
envoy_cc_library(
    name = "postgres_protocol_lib",
    hdrs = ["postgres_protocol.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
)
