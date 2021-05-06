load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "feature_interface",
    hdrs = ["feature.h"],
    deps = [
        "//source/extensions/filters/network/redis_proxy/feature/hotkey:hotkey_interface",
    ],
)

envoy_cc_library(
    name = "feature_lib",
    srcs = ["feature.cc"],
    hdrs = ["feature.h"],
    deps = [
        ":feature_interface",
        "//source/extensions/filters/network/redis_proxy/feature/hotkey:hotkey_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)
