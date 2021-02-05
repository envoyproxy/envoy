load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# StartTls transport socket.

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = (
        "envoy.transport_sockets.downstream",
        "envoy.transport_sockets.upstream",
    ),
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    visibility = ["//visibility:public"],
    deps = [
        ":starttls_socket_lib",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "starttls_socket_lib",
    srcs = ["starttls_socket.cc"],
    hdrs = ["starttls_socket.h"],
    external_deps = [
        "abseil_optional",
        "abseil_synchronization",
        "ssl",
    ],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_annotations",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
    ],
)
