load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "udp_proxy_filter_lib",
    srcs = ["udp_proxy_filter.cc"],
    hdrs = ["udp_proxy_filter.h"],
    deps = [
        "//include/envoy/event:file_event_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":udp_proxy_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)
