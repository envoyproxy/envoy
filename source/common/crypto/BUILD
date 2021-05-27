load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    hdrs = [
        "utility.h",
    ],
    deps = [
        "//include/envoy/buffer:buffer_interface_no_include_prefix",
        "//include/envoy/common/crypto:crypto_interface_no_include_prefix",
        "//source/common/common:assert_lib",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
