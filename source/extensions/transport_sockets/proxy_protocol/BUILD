load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension(
    name = "upstream_proxy_protocol",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    security_posture = "robust_to_untrusted_downstream",
    undocumented = True,
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
