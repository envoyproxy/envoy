load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "consistent_hashing_lib",
    hdrs = ["matcher.h"],
    deps = [
        "//include/envoy/matcher:matcher_interface",
        "//include/envoy/upstream:retry_interface",
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.matching.input_matchers",
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":consistent_hashing_lib",
        "//include/envoy/matcher:matcher_interface",
        "//include/envoy/registry",
        "//include/envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/input_matchers/consistent_hashing/v3:pkg_cc_proto",
    ],
)
