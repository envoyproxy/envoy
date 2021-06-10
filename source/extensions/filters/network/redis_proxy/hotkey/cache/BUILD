load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_interface",
    hdrs = ["cache.h"],
)

envoy_cc_library(
    name = "cache_factory_lib",
    hdrs = ["cache_factory.h"],
    deps = [
        "//source/extensions/filters/network/redis_proxy/hotkey/cache/lfucache:lfu_cache_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)
