load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "test",
    srcs = [
        "EngineBuilderTests.swift",
        "GRPCRequestHeadersBuilderTests.swift",
        "GRPCStreamTests.swift",
        "HeadersBuilderTests.swift",
        "HeadersContainerTests.swift",
        "PulseClientImplTests.swift",
        "RequestHeadersBuilderTests.swift",
        "ResponseHeadersTests.swift",
        "RetryPolicyMapperTests.swift",
        "RetryPolicyTests.swift",
    ],
    flaky = True,  # TODO(jpsim): Fix timeouts when running these tests on CI
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
