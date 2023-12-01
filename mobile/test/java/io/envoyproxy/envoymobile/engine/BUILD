load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test", "envoy_mobile_jni_kt_test", "envoy_mobile_kt_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_jni_kt_test(
    name = "envoy_configuration_test",
    srcs = [
        "EnvoyConfigurationTest.kt",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "@maven//:com_google_protobuf_protobuf_javalite",
    ],
)

envoy_mobile_kt_test(
    name = "envoy_native_resource_registry_test",
    srcs = [
        "EnvoyNativeResourceRegistryTest.kt",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)

envoy_mobile_kt_test(
    name = "jvm_bridge_utility_test",
    srcs = [
        "JvmBridgeUtilityTest.kt",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)

envoy_mobile_android_test(
    name = "android_network_monitor_tests",
    srcs = [
        "AndroidNetworkMonitorTest.java",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
