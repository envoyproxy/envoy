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
        "//include/envoy/buffer:buffer_interface_no_include_prefix",
        "//include/envoy/common:conn_pool_interface_no_include_prefix",
        "//include/envoy/event:deferred_deletable_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "upstream_interface",
    hdrs = ["upstream.h"],
    deps = [
        "//include/envoy/tcp:conn_pool_interface_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
