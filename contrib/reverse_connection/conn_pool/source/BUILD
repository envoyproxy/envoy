load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    visibility = ["//visibility:public"],
    alwayslink = 1,
    deps = [
        ":reversed_connection_lib",
        "//envoy/registry",
        "//envoy/event:dispatcher_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/network:connection_impl",
        "//source/common/http/http2:conn_pool_lib",
    ],
)

envoy_cc_library(
    name = "reversed_connection_lib",
    srcs = ["reversed_connection_impl.cc"],
    hdrs = ["reversed_connection_impl.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/reverse_connection/bootstrap/source:reverse_conn_global_registry",
        "//source/common/network:connection_impl",
    ],
)