load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "string_returning_action_lib",
    srcs = ["string_returning_action.cc"],
    hdrs = ["string_returning_action.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
