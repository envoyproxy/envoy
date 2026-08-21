load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tcp_dns_filter_lib",
    srcs = ["tcp_dns_filter.cc"],
    hdrs = ["tcp_dns_filter.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tcp_dns_filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/tcp/dns_filter/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
    ],
)
