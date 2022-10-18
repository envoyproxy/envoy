load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")
load("//bazel:android_debug_info.bzl", "android_debug_info")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_package()

cc_library(
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
    deps = [
        "//library/common/jni/import:jni_import_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:assert_lib",
    ],
)

cc_library(
    name = "envoy_jni_lib",
    srcs = [
        "android_jni_interface.cc",
        "jni_interface.cc",
    ],
    copts = ["-std=c++17"],
    linkopts = [
        "-lm",
        "-llog",
        "-ldl",
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }),
    deps = [
        ":android_network_utility_lib",
        ":jni_utility_lib",
        ":ndk_jni_support",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

config_setting(
    name = "android_armeabi",
    constraint_values = [
        "@platforms//cpu:armv7",
        "@platforms//os:android",
    ],
)

# Main dynamic library for the Envoy Mobile aar
cc_binary(
    name = "libenvoy_jni.so",
    copts = ["-std=c++17"],
    linkopts = [
        "-lm",
        "-llog",
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }) + select({
        # TODO(keith): https://github.com/rust-lang/compiler-builtins/issues/353
        ":android_armeabi": ["-Wl,--allow-multiple-definition"],
        "//conditions:default": [],
    }),
    linkshared = True,
    deps = [
        ":envoy_jni_lib",
        "@envoy_mobile_extra_jni_deps//:extra_jni_dep",
    ],
)

android_debug_info(
    name = "libenvoy_jni.so.debug_info",
    dep = "libenvoy_jni.so",
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
    ],
    copts = ["-std=c++17"],
    linkopts = [
    ],
    linkshared = True,
    deps = [
        "base_java_jni_lib",
        ":android_network_utility_lib",
        ":java_jni_support",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/jni/import:jni_import_lib",
    ],
)

# OS X binary (.jnilib) for JVM testing
envoy_mobile_so_to_jni_lib(
    name = "java_jni_lib.jnilib",
    native_dep = "libjava_jni_lib.so",
)

cc_library(
    name = "java_jni_base_lib",
    srcs = [
        "jni_interface.cc",
    ],
    deps = [
        "base_java_jni_lib",
        ":android_network_utility_lib",
    ],
    alwayslink = True,
)

# Base binary (.so) for JVM testing
cc_binary(
    name = "libjava_jni_lib.so",
    copts = ["-std=c++17"],
    linkopts = [
        "-lm",
    ],
    linkshared = True,
    deps = [
        ":java_jni_base_lib",
        "@envoy_mobile_extra_jni_deps//:extra_jni_dep",
    ],
)

cc_library(
    name = "base_java_jni_lib",
    srcs = [
        "jni_utility.cc",
        "jni_version.cc",
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
        "//library/common/jni/import:jni_import_lib",
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
    ],
    hdrs = ["jni_support.h"],
    copts = ["-std=c++14"],
    linkopts = [
        "-lm",
    ],
    deps = [
        "//library/common/jni/import:jni_import_lib",
    ],
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
    deps = [
        "//library/common/jni/import:jni_import_lib",
    ],
)

cc_library(
    name = "android_jni_utility_lib",
    srcs = [
        "android_jni_utility.cc",
    ],
    hdrs = [
        "android_jni_utility.h",
    ],
    deps = [
        "@envoy//source/common/common:assert_lib",
        "//library/common/types:c_types_lib",
    ] + select({
        "@envoy//bazel:android": [
            ":ndk_jni_support",
            ":jni_utility_lib",
            "//library/common/data:utility_lib",
            "//library/common/jni/import:jni_import_lib",
        ],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "android_network_utility_lib",
    srcs = [
        "android_network_utility.cc",
    ],
    hdrs = [
        "android_network_utility.h",
    ],
    deps = [
        ":jni_utility_lib",
        "//library/common/api:c_types",
        "//library/common/data:utility_lib",
        "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/types:c_types_lib",
        "@envoy//bazel:boringssl",
        "@envoy//source/common/common:assert_lib",
    ],
)
