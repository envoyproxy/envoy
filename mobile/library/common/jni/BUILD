load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")
load("//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")

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

# Main dynamic library for the Envoy Mobile aar
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
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": ["-Wl,-s"],
    }),
    linkshared = True,
    deps = [
        ":jni_utility_lib",
        ":ndk_jni_support",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
    ],
)

## Targets for local execution
# OS X binary (.jnilib) for NDK testing
envoy_mobile_so_to_jni_lib(
    name = "libndk_envoy_jni.jnilib",
    native_dep = "libndk_envoy_jni.so",
)

# Base binary (.so) for NDK testing
cc_binary(
    name = "libndk_envoy_jni.so",
    srcs = [
        "android_test_jni_interface.cc",
        "jni_interface.cc",
        "@local_jdk//:jni_header",
    ],
    copts = ["-std=c++17"],
    linkopts = [
    ],
    linkshared = True,
    deps = [
        "base_java_jni_lib",
        ":java_jni_support",
        "//bazel:jni",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
    ],
)

# OS X binary (.jnilib) for JVM testing
envoy_mobile_so_to_jni_lib(
    name = "java_jni_lib.jnilib",
    native_dep = "libjava_jni_lib.so",
)

# Base binary (.so) for JVM testing
cc_binary(
    name = "libjava_jni_lib.so",
    srcs = ["jni_interface.cc"],
    copts = ["-std=c++17"],
    linkopts = [
        "-lm",
    ],
    linkshared = True,
    deps = ["base_java_jni_lib"],
)

cc_library(
    name = "base_java_jni_lib",
    srcs = [
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
    ] + TEST_EXTENSIONS,
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
