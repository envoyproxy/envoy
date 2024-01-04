load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_defines", "envoy_mobile_package")
load("//bazel:android_debug_info.bzl", "android_debug_info")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# TODO(jpsim): Migrate these to use `envoy_cc_library`

# Support for JNI, using NDK on Android and Java otherwise, since the JNI APIs
# differ a bit between the two.
cc_library(
    name = "jni_support_lib",
    srcs = select({
        "@envoy//bazel:android": ["ndk_jni_support.cc"],
        "//conditions:default": ["java_jni_support.cc"],
    }),
    hdrs = ["jni_support.h"],
    linkopts = select({
        "@envoy//bazel:android": ["-llog"],
        "//conditions:default": [],
    }),
    deps = [
        "//library/common/jni/import:jni_import_lib",
    ],
)

# Various JNI helper libraries.
cc_library(
    name = "jni_utility_lib",
    srcs = [
        "jni_utility.cc",
    ],
    hdrs = [
        "jni_utility.h",
    ],
    deps = [
        ":jni_helper_lib",
        ":jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_env_lib",
        "//library/common/jni/types:jni_exception_lib",
        "//library/common/types:c_types_lib",
        "//library/common/types:managed_types_lib",
        "//library/common/types:matcher_data_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/protobuf",
    ],
)

cc_library(
    name = "jni_helper_lib",
    srcs = [
        "jni_helper.cc",
    ],
    hdrs = [
        "jni_helper.h",
    ],
    deps = [
        "//library/common/jni/import:jni_import_lib",
        "@envoy//source/common/common:assert_lib",
    ],
)

# Implementations of the various "native" Java methods for classes
# in library/java/io/envoyproxy/envoymobile.
cc_library(
    name = "jni_impl_lib",
    srcs = [
        "jni_impl.cc",
    ],
    defines = envoy_mobile_defines("@envoy"),
    deps = [
        ":android_network_utility_lib",
        ":jni_utility_lib",
        "//library/cc:engine_builder_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/api:c_types",
        "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
        "//library/common/extensions/key_value/platform:config",
        "//library/common/jni/types:jni_exception_lib",
        "//library/common/jni/types:jni_javavm_lib",
        "//library/common/types:managed_types_lib",
        "@envoy//source/common/protobuf",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

# Cert verification related functions which call into AndroidNetworkLibrary.
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
        "//library/common/jni/types:jni_exception_lib",
        "//library/common/jni/types:jni_javavm_lib",
        "//library/common/types:c_types_lib",
        "@envoy//bazel:boringssl",
    ],
)

# Implementations of the "native" Java methods for AndroidJniLibrary.java
cc_library(
    name = "android_jni_impl_lib",
    srcs = [
        "android_jni_impl.cc",
    ],
    deps = [
        ":android_network_utility_lib",
        ":jni_impl_lib",
        ":jni_support_lib",
        ":jni_utility_lib",
        "//library/common:envoy_main_interface_lib",
        "//library/common/jni/import:jni_import_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

# Library which contains all the JNI related targets.
cc_library(
    name = "envoy_jni_lib",
    linkopts = select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }),
    deps = [
        ":android_jni_impl_lib",
        ":android_network_utility_lib",
        ":jni_impl_lib",
        ":jni_support_lib",
        ":jni_utility_lib",
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
        # Without this option the app crashes when launched
        # on arm64 emulator which is not covered by CI.
        "-ldl",
    ] + select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
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

cc_library(
    name = "android_jni_utility_lib",
    srcs = [
        "android_jni_utility.cc",
    ],
    hdrs = [
        "android_jni_utility.h",
    ],
    deps = [
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:assert_lib",
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
