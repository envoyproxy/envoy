load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")
load("@envoy_mobile//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "test",
    srcs = [
        "CancelStreamTest.swift",
        "DirectResponseContainsHeadersMatchIntegrationTest.swift",
        "DirectResponseExactHeadersMatchIntegrationTest.swift",
        "DirectResponseExactPathMatchIntegrationTest.swift",
        "DirectResponseFilterMutationIntegrationTest.swift",
        "DirectResponsePrefixHeadersMatchIntegrationTest.swift",
        "DirectResponsePrefixPathMatchIntegrationTest.swift",
        "DirectResponseSuffixHeadersMatchIntegrationTest.swift",
        "DrainConnectionsTest.swift",
        "EngineApiTest.swift",
        "FilterResetIdleTest.swift",
        "GRPCReceiveErrorTest.swift",
        "IdleTimeoutTest.swift",
        "ReceiveDataTest.swift",
        "ReceiveErrorTest.swift",
        "SendDataTest.swift",
        "SendHeadersTest.swift",
        "SendTrailersTest.swift",
        "SetEventTrackerTest.swift",
        "SetEventTrackerTestNoTracker.swift",
        "SetLoggerTest.swift",
        "StatFlushIntegrationTest.swift",
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
