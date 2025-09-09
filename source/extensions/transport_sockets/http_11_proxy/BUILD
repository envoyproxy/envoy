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
    extra_visibility = [
        "//test/common/upstream:__subpackages__",
    ],
    deps = [
        ":connect",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connect",
    srcs = ["connect.cc"],
    hdrs = ["connect.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:header_utility_lib",
        "//source/common/http/http1:balsa_parser_lib",
        "//source/common/http/http1:legacy_parser_lib",
        "//source/common/network:address_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
    ],
)
