load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "stats_action_lib",
    srcs = ["stats_action.cc"],
    hdrs = ["stats_action.h"],
    visibility = [
        "//source/extensions/access_loggers/stats:__subpackages__",
        "//test/common/stats:__subpackages__",
        "//test/extensions/matching/actions/stats:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/stats:stats_interface",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/matching/actions/stats/v3:pkg_cc_proto",
    ],
)
