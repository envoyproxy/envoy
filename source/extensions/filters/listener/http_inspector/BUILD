load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP inspector filter for sniffing HTTP protocol and setting HTTP version to a FilterChain.

envoy_extension_package()

envoy_cc_library(
    name = "http_inspector_lib",
    srcs = ["http_inspector.cc"],
    hdrs = ["http_inspector.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:legacy_parser_lib",
        "//source/common/http/http1:parser_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":http_inspector_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/listener/http_inspector/v3:pkg_cc_proto",
    ],
)
