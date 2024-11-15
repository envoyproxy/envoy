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
    deps = [
        ":upstream_proxy_protocol",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/proxy_protocol/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_proxy_protocol",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
