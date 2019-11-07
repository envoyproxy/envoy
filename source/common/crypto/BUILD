licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    hdrs = [
        "utility.h",
    ],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/common/crypto:crypto_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:stack_array",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
