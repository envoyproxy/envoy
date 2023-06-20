load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_mobile_android_test(
    name = "android_engine_start_test",
    srcs = [
        "AndroidEnvoyEngineStartUpTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
        "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "android_engine_flow_test",
    srcs = [
        "AndroidEnvoyFlowTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
        "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "android_engine_explicit_flow_test",
    srcs = [
        "AndroidEnvoyExplicitFlowTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
        "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)

envoy_mobile_android_test(
    name = "android_engine_socket_tag_test",
    srcs = [
        "AndroidEngineSocketTagTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
        "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        "//test/java/io/envoyproxy/envoymobile/engine/testing",
    ],
)
