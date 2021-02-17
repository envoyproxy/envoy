load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    deps = [
        ":quic_client_connection_factory_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/http/http2:conn_pool_lib",
    ],
)

envoy_cc_library(
    name = "quic_codec_factory_lib",
    hdrs = ["quic_codec_factory.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
    ],
)

envoy_cc_library(
    name = "quic_client_connection_factory_lib",
    hdrs = ["quic_client_connection_factory.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/ssl:context_config_interface",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = ["//source/common/singleton:const_singleton"],
)
