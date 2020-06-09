load("//bazel:swift_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

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
        "MockEnvoyEngine.swift",
        "MockEnvoyHTTPStream.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
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
    name = "stream_client_builder_tests",
    srcs = [
        "MockEnvoyEngine.swift",
        "MockEnvoyHTTPStream.swift",
        "StreamClientBuilderTests.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
