load("@envoy_mobile//bazel:swift_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "engine_builder_tests",
    srcs = [
        "EngineBuilderTests.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_request_headers_builder_tests",
    srcs = [
        "GRPCRequestHeadersBuilderTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_stream_tests",
    srcs = [
        "GRPCStreamTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "headers_builder_tests",
    srcs = [
        "HeadersBuilderTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "request_headers_builder_tests",
    srcs = [
        "RequestHeadersBuilderTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "response_headers_tests",
    srcs = [
        "ResponseHeadersTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "retry_policy_tests",
    srcs = [
        "RetryPolicyTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "retry_policy_mapper_tests",
    srcs = [
        "RetryPolicyMapperTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "stats_client_impl_tests",
    srcs = [
        "StatsClientImplTests.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "counter_tests",
    srcs = [
        "CounterTests.swift",
    ],
)
