load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = [
        "crypto_impl.cc",
        "utility_impl.cc",
    ],
    hdrs = [
        "crypto_impl.h",
        "utility.h",
        "utility_impl.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common/crypto:crypto_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:threadsafe_singleton",
    ],
    # for the singleton
    alwayslink = 1,
)
