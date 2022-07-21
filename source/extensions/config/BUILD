load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "store_lib",
    srcs = ["store.cc"],
    hdrs = ["store.h"],
    external_deps = ["abseil_optional"],
    # TODO(abeyad): get rid of this
    visibility = ["//visibility:public"],
    deps = [
        ":saved_xds_config_proto_cc_proto",
        "//envoy/common:key_value_store_interface",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "saved_xds_config_proto",
    srcs = ["saved_xds_config.proto"],
    deps = [
        "@envoy_api//envoy/service/discovery/v3:pkg",
    ],
)