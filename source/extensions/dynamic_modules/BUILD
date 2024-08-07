load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = [
        "dynamic_modules.cc",
    ],
    hdrs = [
        "dynamic_modules.h",
    ],
    deps = [
        "//envoy/common:exception_lib",
    ],
)
