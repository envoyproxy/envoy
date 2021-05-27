load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "statsd_lib",
    srcs = ["statsd.cc"],
    hdrs = ["statsd.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/local_info:local_info_interface_no_include_prefix",
        "//include/envoy/network:connection_interface_no_include_prefix",
        "//include/envoy/stats:stats_interface_no_include_prefix",
        "//include/envoy/thread_local:thread_local_interface_no_include_prefix",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
    ],
)
