load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "reverse_connection_utility_lib",
    srcs = ["reverse_connection_utility.cc"],
    hdrs = ["reverse_connection_utility.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/tls:ssl_handshaker_lib",
    ],
)

envoy_cc_library(
    name = "rping_interceptor_lib",
    srcs = ["rping_interceptor.cc"],
    hdrs = ["rping_interceptor.h"],
    deps = [
        ":reverse_connection_utility_lib",
        "//source/common/network:default_socket_interface_lib",
    ],
)
