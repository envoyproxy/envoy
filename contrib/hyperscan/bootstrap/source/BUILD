load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//conditions:default": [
            "HYPERSCAN_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:regex_engine_lib",
        "@envoy_api//contrib/envoy/extensions/bootstrap/hyperscan/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            "//contrib/hyperscan/matching/input_matchers/source:matcher_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
