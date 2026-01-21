load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "pkg",
    srcs = ["v3/async_example.proto"],
    deps = [],
)

envoy_cc_library(
    name = "async_example_lib",
    srcs = ["async_example.cc"],
    hdrs = ["async_example.h"],
    deps = [
        ":pkg_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":async_example_lib",
        ":pkg_cc_proto",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
