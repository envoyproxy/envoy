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
        "//source/common/quic:client_connection_factory_lib",
    ],
)

envoy_cc_library(
    name = "quic_client_connection_factory_lib",
    hdrs = ["quic_client_connection_factory.h"],
)
