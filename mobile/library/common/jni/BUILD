load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")
load("//bazel:android_debug_info.bzl", "android_debug_info")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_package()

# Suport for JNI using NDK on android and Java otherwise.
cc_library(
    name = "jni_support_lib",
    srcs = [
    ] + select({
        "@envoy//bazel:android": ["ndk_jni_support.cc"],
        "//conditions:default": ["java_jni_support.cc"],
    }),
    hdrs = ["jni_support.h"],
    linkopts = [
        "-lm",
    ] + select({
        "@envoy//bazel:android": ["-llog"],
        "//conditions:default": [],
    }),
    deps = [
        "//library/common/jni/import:jni_import_lib",
    ],
)

cc_library(
    name = "jni_utility_lib",
    srcs = [
        "jni_utility.cc",
        "jni_version.cc",
    ],
    hdrs = [
        "jni_utility.h",
        "jni_version.h",
    ],
    deps = [
        ":jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/types:c_types_lib",
        "//library/common/types:managed_types_lib",
        "@envoy//source/common/common:assert_lib",
    ],
)

cc_library(
    name = "jni_interface_lib",
    srcs = [
        "jni_interface.cc",
    ],
    deps = [
        ":android_network_utility_lib",
        ":jni_utility_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/extensions/key_value/platform:config",
        "//library/common/types:managed_types_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

cc_library(
    name = "envoy_jni_lib",
    srcs = [
        "android_jni_interface.cc",
    ],
    linkopts = [
        "-lm",
        "-ldl",
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }) + select({
        "@envoy//bazel:android": ["-llog"],
        "//conditions:default": [],
    }),
    deps = [
        ":android_network_utility_lib",
        ":jni_interface_lib",
        ":jni_support_lib",
        ":jni_utility_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/types:managed_types_lib",
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
    linkopts = [
        "-lm",
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }) + select({
        "@envoy//bazel:android": ["-llog"],
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
    name = "libndk_envoy_jni_jnilib",
    native_dep = "libndk_envoy_jni.so",
)

# Base binary (.so) for NDK testing
cc_binary(
    name = "libndk_envoy_jni.so",
    srcs = [
        "android_test_jni_interface.cc",
    ],
    linkopts = [
    ],
    linkshared = True,
    deps = [
        ":android_network_utility_lib",
        ":base_java_jni_lib",
        ":jni_interface_lib",
        ":jni_support_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/types:managed_types_lib",
    ],
)

# OS X binary (.jnilib) for JVM testing
envoy_mobile_so_to_jni_lib(
    name = "java_jni_lib.jnilib",
    native_dep = "libjava_jni_lib.so",
)

cc_library(
    name = "java_jni_base_lib",
    deps = [
        ":android_network_utility_lib",
        ":base_java_jni_lib",
        ":jni_interface_lib",
    ],
    alwayslink = True,
)

# Base binary (.so) for JVM testing
cc_binary(
    name = "libjava_jni_lib.so",
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
    linkopts = [
        "-lm",
    ],
    deps = [
        ":jni_support_lib",
        ":jni_utility_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/types:c_types_lib",
        "//library/common/types:managed_types_lib",
        "@envoy//source/common/common:assert_lib",
    ] + TEST_EXTENSIONS,
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
            ":jni_support_lib",
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
