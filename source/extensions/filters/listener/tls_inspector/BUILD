licenses(["notice"])  # Apache 2
# TLS inspector filter for examining various TLS parameters before routing to a FilterChain.
# Public docs: docs/root/configuration/listener_filters/tls_inspector.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tls_inspector_lib",
    srcs = ["tls_inspector.cc"],
    hdrs = ["tls_inspector.h"],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/transport_sockets:well_known_names",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
    ],
)
