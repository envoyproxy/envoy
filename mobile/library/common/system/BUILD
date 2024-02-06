load("@bazel_skylib//lib:selects.bzl", "selects")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

selects.config_setting_group(
    name = "use_android_system_helper",
    match_any = [
        "@envoy//bazel:android_system_helper",
        "@envoy//bazel:android",
    ],
)

envoy_cc_library(
    name = "system_helper_lib",
    srcs = [
        "system_helper.cc",
    ],
    hdrs = [
        "system_helper.h",
    ],
    copts = select({
        ":use_android_system_helper": ["-DUSE_ANDROID_SYSTEM_HELPER"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = [
        ":default_system_helper_lib",
        "//library/common/extensions/cert_validator/platform_bridge:c_types_lib",
    ] + select({
        ":use_android_system_helper": [
            "//library/jni:android_jni_utility_lib",
            "//library/jni:android_network_utility_lib",
        ],
        "//conditions:default": [],
    }) + select({
        "@envoy//bazel:apple": [
            "//library/common/network:apple_platform_cert_verifier",
        ],
        "//conditions:default": [],
    }),
)

# Ideally, Bazel select() would choose the appropriate implementation to use.
# It would use the Android implementation on Android, or when the config
# android_system_helper is set. Otherwise, it would use the Apple implementation
# on Apple platforms, or the default otherwise. However, Bazel select() doesn't
# have a good way to express this. Instead, system_helper.cc uses "#if defined"
# and "#include" to select the correct implementation.
envoy_cc_library(
    name = "default_system_helper_lib",
    srcs = [],
    hdrs = [
        "default_system_helper.cc",
        "default_system_helper.h",
        "default_system_helper_android.cc",
        "default_system_helper_apple.cc",
    ],
    repository = "@envoy",
    deps = [],
)
