load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "fragment",
    srcs = ["fragment.cc"],
    hdrs = ["fragment.h"],
    deps = [
        "//source/extensions/common/async_files",
    ],
)
