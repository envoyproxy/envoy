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
    deps = [
        ":starttls_socket_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "starttls_socket_lib",
    srcs = ["starttls_socket.cc"],
    hdrs = ["starttls_socket.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_annotations",
        "//source/common/network:transport_socket_options_lib",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
    ],
)
