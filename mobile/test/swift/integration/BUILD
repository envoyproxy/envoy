load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.swift",
        "DemoFilter.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_contains_headers_integration_test",
    srcs = [
        "DirectResponseContainsHeadersMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_exact_headers_match_integration_test",
    srcs = [
        "DirectResponseExactHeadersMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_exact_path_match_integration_test",
    srcs = [
        "DirectResponseExactPathMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_filter_mutation_integration_test",
    srcs = [
        "DirectResponseFilterMutationIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_prefix_headers_match_integration_test",
    srcs = [
        "DirectResponsePrefixHeadersMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_prefix_path_match_integration_test",
    srcs = [
        "DirectResponsePrefixPathMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "direct_response_suffix_path_match_integration_test",
    srcs = [
        "DirectResponseSuffixHeadersMatchIntegrationTest.swift",
    ],
)

envoy_mobile_swift_test(
    name = "receive_data_test",
    srcs = [
        "DemoFilter.swift",
        "ReceiveDataTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "receive_error_test",
    srcs = [
        "DemoFilter.swift",
        "ReceiveErrorTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_receive_error_test",
    srcs = [
        "DemoFilter.swift",
        "GRPCReceiveErrorTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_data_test",
    srcs = [
        "DemoFilter.swift",
        "SendDataTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_headers_test",
    srcs = [
        "DemoFilter.swift",
        "SendHeadersTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_trailers_test",
    srcs = [
        "DemoFilter.swift",
        "SendTrailersTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "set_logger_test",
    srcs = [
        "SetLoggerTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
