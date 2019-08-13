licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "previous_priorities_lib",
    srcs = ["previous_priorities.cc"],
    hdrs = ["previous_priorities.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
        "//source/common/upstream:load_balancer_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":previous_priorities_lib",
        "//include/envoy/registry",
        "//include/envoy/upstream:retry_interface",
        "//source/common/protobuf",
        "//source/extensions/retry/priority:well_known_names",
        "@envoy_api//envoy/config/retry/previous_priorities:previous_priorities_cc",
    ],
)
