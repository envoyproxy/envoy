load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "regex_lib",
    srcs = ["regex.cc"],
    hdrs = ["regex.h"],
    deps = [
        "//envoy/common:regex_interface",
    ] + select({
        "//bazel:linux_x86_64": [
            "//contrib/hyperscan/matching/input_matchers/source:hyperscan_matcher_lib",
        ],
        "//bazel:linux_aarch64": [
            "//contrib/hyperscan/matching/input_matchers/source:vectorscan_matcher_lib",
        ],
    }),
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
        "//envoy/common:regex_interface",
        "@envoy_api//contrib/envoy/extensions/regex_engines/hyperscan/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":regex_lib",
        ],
        "//bazel:linux_aarch64": [
            ":regex_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
