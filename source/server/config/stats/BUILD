licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tcp_statsd_lib",
    srcs = ["tcp_statsd.cc"],
    hdrs = ["tcp_statsd.h"],
    deps = [
        "//source/common/stats:statsd_lib",
        "//source/server:configuration_lib",
    ],
)

envoy_cc_library(
    name = "udp_statsd_lib",
    srcs = ["udp_statsd.cc"],
    hdrs = ["udp_statsd.h"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/stats:statsd_lib",
        "//source/server:configuration_lib",
    ],
)
