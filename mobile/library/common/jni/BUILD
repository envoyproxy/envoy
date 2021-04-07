load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
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
        "jni_support.h",
        "jni_utility.h",
        "jni_version.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:assert_lib",
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
        ":ndk_jni_support",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
    ],
)

# Java JNI library
cc_library(
    name = "java_jni_lib",
    srcs = [
        "jni_interface.cc",
        "jni_utility.cc",
        "jni_version.cc",
        "@local_jdk//:jni_header",
    ],
    hdrs = [
        "jni_utility.h",
        "jni_version.h",
    ],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
    ],
    deps = [
        ":java_jni_support",
        "//bazel:jni",
        "//library/common:envoy_main_interface_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:assert_lib",
    ],
)

# Utility implementation for Java JNI
cc_library(
    name = "java_jni_support",
    srcs = [
        "java_jni_support.cc",
        "@local_jdk//:jni_header",
    ],
    hdrs = ["jni_support.h"],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
    ],
    deps = ["//bazel:jni"],
)

# Utility implementation for NDK JNI
cc_library(
    name = "ndk_jni_support",
    srcs = [
        "ndk_jni_support.cc",
    ],
    hdrs = ["jni_support.h"],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
        "-llog",
    ],
)
