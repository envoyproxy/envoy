load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "upstream_config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",  # header generated in Envoy, so can't be faked
    deps = [
        ":upstream_proxy_protocol",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/extensions/transport_sockets/proxy_protocol/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_proxy_protocol",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
