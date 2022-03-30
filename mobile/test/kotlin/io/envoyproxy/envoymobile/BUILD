load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_kt_test")

licenses(["notice"])  # Apache 2

envoy_mobile_kt_test(
    name = "engine_builder_test",
    srcs = [
        "EngineBuilderTest.kt",
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
    name = "flatbuffers_test",
    srcs = [
        "FlatBuffersTest.kt",
    ],
    deps = [
        "//test/fbs:test_fb_kt_lib",
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
