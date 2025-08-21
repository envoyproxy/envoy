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
        "//envoy/buffer:buffer_interface",
        "//envoy/common:conn_pool_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "upstream_interface",
    hdrs = ["upstream.h"],
    deps = [
        "//envoy/http:header_evaluator",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "async_tcp_client_interface",
    hdrs = ["async_tcp_client.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
    ],
)
