load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP inspector filter for sniffing HTTP protocol and setting HTTP version to a FilterChain.

envoy_package()

envoy_cc_library(
    name = "http_inspector_lib",
    srcs = ["http_inspector.cc"],
    hdrs = ["http_inspector.h"],
    external_deps = ["http_parser"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/transport_sockets:well_known_names",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":http_inspector_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
        "@envoy_api//envoy/extensions/filters/listener/http_inspector/v3:pkg_cc_proto",
    ],
)
