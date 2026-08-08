load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "hyperscan_matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/common:regex_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:empty_string",
        "@hyperscan",
    ],
)

envoy_cc_library(
    name = "vectorscan_matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/common:regex_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "@vectorscan",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//bazel:linux_aarch64": [],
        "//conditions:default": [
            "HYPERSCAN_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":hyperscan_matcher_lib",
        ],
        "//bazel:linux_aarch64": [
            ":vectorscan_matcher_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
