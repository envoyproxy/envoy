load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "lru_cache_lib",
    hdrs = [
        "lru_cache.h",
        "lru_cache_inl.h",
    ],
)
