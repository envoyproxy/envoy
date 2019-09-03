licenses(["notice"])  # Apache 2

load("//bazel:swift_test.bzl", "envoy_mobile_swift_test")

envoy_mobile_swift_test(
    name = "envoy_tests",
    srcs = [
        "EnvoyTests.swift",
        "MockEnvoyHTTPStream.swift",
    ],
)

envoy_mobile_swift_test(
    name = "envoy_builder_tests",
    srcs = [
        "EnvoyBuilderTests.swift",
        "MockEnvoyHTTPStream.swift",
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
