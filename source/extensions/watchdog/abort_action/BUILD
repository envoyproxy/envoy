load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "abort_action_lib",
    srcs = ["abort_action.cc"],
    hdrs = ["abort_action.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/server:guarddog_config_interface",
        "//include/envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        ":abort_action_lib",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/extensions/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)
