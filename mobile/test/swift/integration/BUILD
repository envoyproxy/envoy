load("@rules_cc//cc:defs.bzl", "objc_library")
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
        "EngineApiTest.swift",
        "FilterResetIdleTest.swift",
        "GRPCReceiveErrorTest.swift",
        "IdleTimeoutTest.swift",
        "ReceiveDataTest.swift",
        "ReceiveErrorTest.swift",
        "ResetConnectivityStateTest.swift",
        "SendDataTest.swift",
        "SendHeadersTest.swift",
        "SendTrailersTest.swift",
        "SetEventTrackerTest.swift",
        "SetEventTrackerTestNoTracker.swift",
        "SetLoggerTest.swift",
        "StatFlushIntegrationTest.swift",
    ],
    # TODO(jpsim): Fix remote execution for these tests
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)

envoy_mobile_swift_test(
    name = "flatbuffer_test",
    srcs = [
        "FlatBufferTest.swift",
    ] + [
        "//test/fbs:test_fb_swift_srcs",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@swift_flatbuffers//:FlatBuffers",
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
