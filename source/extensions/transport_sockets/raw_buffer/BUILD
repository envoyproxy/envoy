load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Built-in plaintext connection transport socket.

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    # This is core Envoy config.
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/config/transport_socket/raw_buffer/v2:pkg_cc_proto",
    ],
)
