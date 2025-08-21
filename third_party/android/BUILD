load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_cc_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "android_lib",
    hdrs =  select({
        "//bazel:android": [
            "ifaddrs-android.h",
            "LocalArray.h",
            "ScopedFd.h",
        ],
        "//conditions:default": [],
    })
)
