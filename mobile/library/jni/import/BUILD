load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "jni_import_lib",
    srcs = [
        "jni_import.h",
    ],
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:linux": ["//bazel:jni"],
        "@envoy//bazel:apple": ["//bazel:jni"],
        "//conditions:default": [],
    }),
)
