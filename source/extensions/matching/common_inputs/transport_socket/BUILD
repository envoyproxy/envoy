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
    visibility = [
        "//source/common/upstream:__pkg__",
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/upstream:transport_socket_matching_data_interface",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:json_utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/transport_socket/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    deps = [":config_lib"],
)
