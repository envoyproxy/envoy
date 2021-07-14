load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "lfu_cache_lib",
    srcs = ["lfu_cache_impl.cc"],
    hdrs = ["lfu_cache.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/extensions/filters/network/redis_proxy/hotkey/cache:cache_interface",
    ],
)
