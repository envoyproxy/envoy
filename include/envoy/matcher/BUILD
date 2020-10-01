load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "matcher_interface",
    hdrs = ["matcher.h"],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        # "abseil_flat_hash_map",
    ],
    deps = ["@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto"],
)
