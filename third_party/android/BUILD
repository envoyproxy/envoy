load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "android_lib",
    hdrs = select({
        "//bazel:android": [
            "LocalArray.h",
            "ScopedFd.h",
            "ifaddrs-android.h",
        ],
        "//conditions:default": [],
    }),
)
