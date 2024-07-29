load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Built-in TLS connection transport socket.

envoy_extension_package()

envoy_cc_library(
    name = "base_config",
    hdrs = ["config.h"],
    deps = [
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/tls:ssl_socket_base",
    ],
)

envoy_cc_library(
    name = "downstream_config",
    srcs = ["downstream_config.cc"],
    hdrs = ["downstream_config.h"],
    deps = [
        ":base_config",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_ssl_socket_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
    alwayslink = True,
)

envoy_cc_library(
    name = "upstream_config",
    srcs = ["upstream_config.cc"],
    hdrs = ["upstream_config.h"],
    deps = [
        ":base_config",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/tls:client_ssl_socket_lib",
        "//source/common/tls:context_config_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    visibility = ["//visibility:public"],
    deps = [
        ":downstream_config",
        ":upstream_config",
    ],
)
