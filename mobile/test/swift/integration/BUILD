load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")
load("@envoy_mobile//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "cancel_stream_test",
    srcs = [
        "CancelStreamTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "drain_connections_test",
    srcs = [
        "DrainConnectionsTest.swift",
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
    name = "stat_flush_integration_test",
    srcs = [
        "StatFlushIntegrationTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
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
    name = "filter_reset_idle_test",
    srcs = [
        "FilterResetIdleTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "idle_timeout_test",
    srcs = [
        "IdleTimeoutTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "receive_data_test",
    srcs = [
        "ReceiveDataTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "receive_error_test",
    srcs = [
        "ReceiveErrorTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "grpc_receive_error_test",
    srcs = [
        "GRPCReceiveErrorTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_data_test",
    srcs = [
        "SendDataTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_headers_test",
    srcs = [
        "SendHeadersTest.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "send_trailers_test",
    srcs = [
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
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_cc_library(
    name = "test_extensions_cc",
    srcs = [
        "test_extensions.cc",
    ],
    hdrs = [
        "test_extensions.h",
    ],
    repository = "@envoy",
    deps = TEST_EXTENSIONS,
)

objc_library(
    name = "test_extensions",
    hdrs = [
        "TestExtensions.h",
    ],
    module_name = "TestExtensions",
    deps = [
        ":test_extensions_cc",
    ],
)

envoy_mobile_swift_test(
    name = "set_event_tracker_test",
    srcs = [
        "SetEventTrackerTest.swift",
    ],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "set_event_tracker_test_no_tracker",
    srcs = [
        "SetEventTrackerTestNoTracker.swift",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
