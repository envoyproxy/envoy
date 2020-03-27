licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "dns_filter_lib",
    srcs = ["dns_filter.cc"],
    hdrs = ["dns_filter.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listener_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/config:config_provider_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:rds_lib",
        "@envoy_api//envoy/config/filter/udp/dns_filter/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    status = "alpha",
    deps = [
        ":dns_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "@envoy_api//envoy/config/filter/udp/dns_filter/v2alpha:pkg_cc_proto",
    ],
)
