licenses(["notice"])  # Apache 2

load("//bazel:swift_test.bzl", "envoy_mobile_swift_test")

envoy_mobile_swift_test(
    name = "envoy_client_tests",
    srcs = [
        "EnvoyClientTests.swift",
        "MockEnvoyEngine.swift",
        "MockEnvoyHTTPStream.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "envoy_client_builder_tests",
    srcs = [
        "EnvoyClientBuilderTests.swift",
        "MockEnvoyEngine.swift",
        "MockEnvoyHTTPStream.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_request_builder_tests",
    srcs = [
        "GRPCRequestBuilderTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_response_handler_tests",
    srcs = [
        "GRPCResponseHandlerTests.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_stream_emitter_tests",
    srcs = [
        "GRPCStreamEmitterTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "request_builder_tests",
    srcs = [
        "RequestBuilderTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "request_mapper_tests",
    srcs = [
        "RequestMapperTests.swift",
    ],
)

envoy_mobile_swift_test(
    name = "response_handler_tests",
    srcs = [
        "ResponseHandlerTests.swift",
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
