load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

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
        "//source/common/thread:terminate_thread_lib",
        "@envoy_api//envoy/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)

# TODO(kbaichoo): is there a more appropriate build unit for this?
envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abort_action_lib",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)
