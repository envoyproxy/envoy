licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "connection_mapper_factory_lib",
    srcs = ["connection_mapper_factory.cc"],
    hdrs = ["connection_mapper_factory.h"],
    deps = [
        ":src_ip_transparent_mapper_lib",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:connection_mapper_interface",
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_library(
    name = "src_ip_transparent_mapper_lib",
    srcs = ["src_ip_transparent_mapper.cc"],
    hdrs = ["src_ip_transparent_mapper.h"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:connection_mapper_interface",
        "//include/envoy/network:address_interface",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "wrapped_connection_pool_lib",
    srcs = ["wrapped_connection_pool.cc"],
    hdrs = ["wrapped_connection_pool.h"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:connection_mapper_interface",
        "//source/common/http:conn_pool_base_lib",
    ],
)
