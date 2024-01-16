load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "runtime_fraction_lib",
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/upstream:retry_interface",
        "//source/common/common:hash_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":runtime_fraction_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/input_matchers/runtime_fraction/v3:pkg_cc_proto",
    ],
)
