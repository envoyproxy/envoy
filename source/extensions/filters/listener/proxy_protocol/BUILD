licenses(["notice"])  # Apache 2
# Proxy protocol V1 listener filter: https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
# Note: Currently there are no public docs for this filter as it is implicitly loaded by
#       configuration options. In the future it will likely be configurable on a per filter chain
#       basis and will need public docs.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "proxy_protocol_lib",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
    ],
)
