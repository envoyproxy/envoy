load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "conn_pool_base_lib",
    srcs = ["conn_pool_base.cc"],
    hdrs = ["conn_pool_base.h"],
    deps = [
        "//include/envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)
