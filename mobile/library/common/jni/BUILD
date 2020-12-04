load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "jni_utility_lib",
    srcs = [
        "jni_utility.cc",
        "jni_version.cc",
    ],
    hdrs = [
        "jni_utility.h",
        "jni_version.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/types:c_types_lib",
    ],
)

cc_binary(
    name = "libenvoy_jni.so",
    srcs = [
        "android_jni_interface.cc",
        "jni_interface.cc",
    ],
    copts = ["-std=c++17"],
    linkopts = [
        "-lm",
        "-llog",
        "-Wl,-s",
    ],
    linkshared = True,
    deps = [
        ":jni_utility_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
    ],
)
