load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_jni_kt_test")

envoy_mobile_jni_kt_test(
    name = "engine_start_test",
    srcs = [
        "EnvoyEngineSimpleIntegrationTest.kt",
    ],
    native_deps = [
        "//library/common/jni:libjava_jni_lib.so",
        "//library/common/jni:java_jni_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "set_logger_test",
    srcs = [
        "SetLoggerTest.kt",
    ],
    native_deps = [
        "//library/common/jni:libjava_jni_lib.so",
        "//library/common/jni:java_jni_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.kt",
    ],
    native_deps = [
        "//library/common/jni:libjava_jni_lib.so",
        "//library/common/jni:java_jni_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "grpc_receive_error_test",
    srcs = [
        "GRPCReceiveErrorTest.kt",
    ],
    native_deps = [
        "//library/common/jni:libjava_jni_lib.so",
        "//library/common/jni:java_jni_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_jni_kt_test(
    name = "receive_data_test",
    srcs = [
        "ReceiveDataTest.kt",
    ],
    native_deps = [
        "//library/common/jni:libjava_jni_lib.so",
        "//library/common/jni:java_jni_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
