load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "hyperscan_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    external_deps = ["hyperscan"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "@envoy_api//contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":hyperscan_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha:pkg_cc_proto",
    ],
)
