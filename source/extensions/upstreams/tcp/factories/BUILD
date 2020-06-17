load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "default_tcp_upstream_factory",
    srcs = [
        "default_tcp_upstream_factory.cc",
    ],
    hdrs = [
        "default_tcp_upstream_factory.h",
    ],
    deps = [
        "//include/envoy/tcp:upstream_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/extensions/upstreams/tcp/http:http_upstream",
        "//source/extensions/upstreams/tcp/tcp:tcp_upstream",
    ],
)
