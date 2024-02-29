load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "statsd_lib",
    srcs = [
        "statsd.cc",
        "tag_formats.cc",
    ],
    hdrs = [
        "statsd.h",
        "tag_formats.h",
    ],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:utility_lib",
    ],
)
