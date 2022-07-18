load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "config_saver",
    srcs = ["config_saver.cc"],
    hdrs = ["config_saver.h"],
    # TODO(abeyad): get rid of this, use a factory to create.
    visibility = ["//visibility:public"],
    deps = [
        ":saved_xds_config_proto_cc_proto",
        "//envoy/common:key_value_store_interface",
        "//envoy/config:config_updated_listener_interface",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:filesystem_interface",
    ],
)

envoy_proto_library(
    name = "saved_xds_config_proto",
    srcs = ["saved_xds_config.proto"],
    deps = [
        "@envoy_api//envoy/service/discovery/v3:pkg",
    ],
)