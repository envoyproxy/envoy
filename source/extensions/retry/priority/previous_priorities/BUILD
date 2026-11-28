load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "previous_priorities_lib",
    srcs = ["previous_priorities.cc"],
    hdrs = ["previous_priorities.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//envoy/upstream:retry_interface",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":previous_priorities_lib",
        "//envoy/registry",
        "//envoy/upstream:retry_interface",
        "//source/common/common:thread_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/runtime:runtime_features_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/retry/priority/previous_priorities/v3:pkg_cc_proto",
    ],
)
