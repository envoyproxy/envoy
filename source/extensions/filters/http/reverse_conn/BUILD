load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_conn_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "reverse_conn_filter_lib",
    srcs = ["reverse_conn_filter.cc"],
    hdrs = ["reverse_conn_filter.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:connection_socket_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_tunnel_initiator_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_handshake/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
    ],
)
