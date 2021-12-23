load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Connect Handler filter for sniffing HTTP protocol and setting HTTP version to a FilterChain.

envoy_extension_package()

envoy_cc_library(
    name = "connect_handler_lib",
    srcs = ["connect_handler.cc"],
    hdrs = ["connect_handler.h"],
    external_deps = ["http_parser"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":connect_handler_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/listener/connect_handler/v3:pkg_cc_proto",
    ],
)
