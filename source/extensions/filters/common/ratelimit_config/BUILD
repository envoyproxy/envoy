load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_config_lib",
    srcs = ["ratelimit_config.cc"],
    hdrs = ["ratelimit_config.h"],
    external_deps = [
        "abseil_strings",
        "abseil_inlined_vector",
    ],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
    ],
)
