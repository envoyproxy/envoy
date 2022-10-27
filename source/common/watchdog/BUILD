load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "abort_action_lib",
    srcs = ["abort_action.cc"],
    hdrs = ["abort_action.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/server:guarddog_config_interface",
        "//envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/thread:terminate_thread_lib",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "abort_action_config",
    srcs = ["abort_action_config.cc"],
    hdrs = ["abort_action_config.h"],
    deps = [
        ":abort_action_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)
