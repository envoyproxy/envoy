load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:envoy_mobile_android_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "envoy_configuration_test",
    srcs = [
        "EnvoyConfigurationTest.kt",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    test_class = "io.envoyproxy.envoymobile.engine.EnvoyConfigurationTest",
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
        "@maven//:com_google_protobuf_protobuf_javalite",
    ],
)

envoy_mobile_android_test(
    name = "envoy_native_resource_registry_test",
    srcs = [
        "EnvoyNativeResourceRegistryTest.kt",
    ],
    test_class = "io.envoyproxy.envoymobile.engine.EnvoyNativeResourceRegistryTest",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)

envoy_mobile_android_test(
    name = "jvm_bridge_utility_test",
    srcs = [
        "JvmBridgeUtilityTest.kt",
    ],
    test_class = "io.envoyproxy.envoymobile.engine.JvmBridgeUtilityTest",
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
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    test_class = "io.envoyproxy.envoymobile.engine.AndroidNetworkMonitorTest",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/kotlin/io/envoyproxy/envoymobile/mocks:mocks_lib",
    ],
)

envoy_mobile_android_test(
    name = "byte_buffers_test",
    srcs = [
        "ByteBuffersTest.java",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    test_class = "io.envoyproxy.envoymobile.engine.ByteBuffersTest",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
