load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "profile_action_lib",
    srcs = ["profile_action.cc"],
    hdrs = ["profile_action.h"],
    external_deps = [
        "abseil_optional",
    ],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/server:guarddog_config_interface",
        "//include/envoy/thread:thread_interface",
        "//source/common/profiler:profiler_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/watchdog/profile_action/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":profile_action_lib",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/extensions/watchdog/profile_action/v3alpha:pkg_cc_proto",
    ],
)
