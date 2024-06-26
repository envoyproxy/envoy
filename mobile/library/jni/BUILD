load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")
load("//bazel:android_debug_info.bzl", "android_debug_info")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "jni_utility_lib",
    srcs = [
        "jni_utility.cc",
    ],
    hdrs = [
        "jni_utility.h",
    ],
    repository = "@envoy",
    deps = [
        ":jni_helper_lib",
        "//library/common/types:c_types_lib",
        "//library/common/types:managed_types_lib",
        "//library/common/types:matcher_data_lib",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "jni_helper_lib",
    srcs = [
        "jni_helper.cc",
    ],
    hdrs = [
        "jni_helper.h",
    ],
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:linux": ["//bazel:jni"],
        "@envoy//bazel:apple": ["//bazel:jni"],
        # On Android, we should use the JNI provided by the system.
        "//conditions:default": [],
    }) + [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@envoy//source/common/common:assert_lib",
    ],
)

# Implementations of the various "native" Java methods for classes
# in library/java/io/envoyproxy/envoymobile.
envoy_cc_library(
    name = "jni_impl_lib",
    srcs = [
        "jni_impl.cc",
    ],
    repository = "@envoy",
    deps = [
        ":android_network_utility_lib",
        ":jni_utility_lib",
        "//library/cc:engine_builder_lib",
        "//library/common:internal_engine_lib",
        "//library/common/api:c_types",
        "//library/common/bridge:utility_lib",
        "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
        "//library/common/extensions/key_value/platform:config",
        "//library/common/types:managed_types_lib",
        "@envoy//source/common/protobuf",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

# Cert verification related functions which call into AndroidNetworkLibrary.
envoy_cc_library(
    name = "android_network_utility_lib",
    srcs = [
        "android_network_utility.cc",
    ],
    hdrs = [
        "android_network_utility.h",
    ],
    repository = "@envoy",
    deps = [
        ":jni_helper_lib",
        ":jni_utility_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
        "//library/common/types:c_types_lib",
        "@envoy//bazel:boringssl",
    ],
)

# Library which contains all the JNI related targets.
envoy_cc_library(
    name = "envoy_jni_lib",
    linkopts = select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = [
        ":android_network_utility_lib",
        ":jni_impl_lib",
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
        # Brotli compressor includes libm, so Envoy itself gets it by default.
        # Envoy Mobile doesn't include Brotli compressor, so libm doesn't get
        # included in the build, but several parts of Envoy rely on libm, for
        # example, for std::pow, so we link it in here.
        "-lm",
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

envoy_cc_library(
    name = "android_jni_utility_lib",
    srcs = [
        "android_jni_utility.cc",
    ],
    hdrs = [
        "android_jni_utility.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:assert_lib",
    ] + select({
        "@envoy//bazel:android": [
            ":jni_helper_lib",
            ":jni_utility_lib",
            "//library/common/bridge:utility_lib",
        ],
        "//conditions:default": [],
    }),
)
