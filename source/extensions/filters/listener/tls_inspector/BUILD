load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# TLS inspector filter for examining various TLS parameters before routing to a FilterChain.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/listener_filters/tls_inspector

envoy_extension_package()

envoy_cc_library(
    name = "tls_inspector_lib",
    srcs = ["tls_inspector.cc"],
    hdrs = ["tls_inspector.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/listener/tls_inspector/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    extra_visibility = [
        "//test/common/listener_manager:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "@envoy_api//envoy/extensions/filters/listener/tls_inspector/v3:pkg_cc_proto",
    ],
)
