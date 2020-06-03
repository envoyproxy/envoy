load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/common:conn_pool_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:upstream_interface",
    ],
)
