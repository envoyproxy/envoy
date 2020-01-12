licenses(["notice"])  # Apache 2

# ProxyProtocol upstream network filter
# Public docs: docs/root/configuration/upstream/network_filters/proxy_protocol.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "proxy_protocol_lib",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    deps = [
        "//include/envoy/network:filter_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/common/proxy_protocol:proxy_protocol_header_lib",
        "@envoy_api//envoy/extensions/filters/network/proxy_protocol/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":proxy_protocol_lib",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/extensions/filters/network/proxy_protocol/v3:pkg_cc_proto",
    ],
)
