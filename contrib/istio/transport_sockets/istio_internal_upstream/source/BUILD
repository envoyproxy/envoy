load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "istio_internal_upstream",
    srcs = ["istio_internal_upstream.cc"],
    hdrs = ["istio_internal_upstream.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "//source/extensions/transport_sockets/internal_upstream:internal_upstream",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":istio_internal_upstream",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "@envoy_api//contrib/envoy/extensions/transport_sockets/istio_internal_upstream/v3:pkg_cc_proto",
    ],
)
