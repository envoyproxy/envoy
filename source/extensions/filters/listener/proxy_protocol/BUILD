load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Proxy protocol listener filter: https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

envoy_extension_package()

envoy_cc_library(
    name = "proxy_protocol_lib",
    srcs = ["proxy_protocol.cc"],
    hdrs = [
        "proxy_protocol.h",
        "proxy_protocol_header.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/filters/listener:well_known_names",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    # TODO(#9953) clean up.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
    ],
)
