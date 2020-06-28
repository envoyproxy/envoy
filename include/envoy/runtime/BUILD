load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "runtime_interface",
    hdrs = ["runtime.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:threadsafe_singleton",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
