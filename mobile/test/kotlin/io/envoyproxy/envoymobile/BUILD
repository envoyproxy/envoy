load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test", "envoy_mobile_kt_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "engine_builder_test",
    srcs = [
        "EngineBuilderTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the JVM paths are allow-listed in the sandbox.
        "sandboxAllowed": "False",
    },
    native_deps = [
        "//test/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "grpc_request_headers_builder_test",
    srcs = [
        "GRPCRequestHeadersBuilderTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "grpc_stream_test",
    srcs = [
        "GRPCStreamTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//test/kotlin/io/envoyproxy/envoymobile/mocks:mocks_lib",
    ],
)

envoy_mobile_kt_test(
    name = "headers_builder_test",
    srcs = [
        "HeadersBuilderTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "pulse_client_impl_test",
    srcs = [
        "PulseClientImplTest.kt",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the JVM paths are allow-listed in the sandbox.
        "sandboxAllowed": "False",
    },
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "request_headers_builder_test",
    srcs = [
        "RequestHeadersBuilderTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "response_headers_test",
    srcs = [
        "ResponseHeadersTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "retry_policy_mapper_test",
    srcs = [
        "RetryPolicyMapperTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "headers_container_test",
    srcs = [
        "HeadersContainerTest.kt",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
