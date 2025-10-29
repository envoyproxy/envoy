load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/network:address_interface",
        "//envoy/network:connection_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:fmt_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:json_utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/transport_socket/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    deps = [":config_lib"],
)
