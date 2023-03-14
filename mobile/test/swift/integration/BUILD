load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

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
        "KeyValueStoreTest.swift",
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
    name = "cancel_grpc_stream_test",
    srcs = [
        "CancelGRPCStreamTest.swift",
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

objc_library(
    name = "test_extensions",
    hdrs = [
        "TestExtensions.h",
    ],
    module_name = "TestExtensions",
    deps = [
        "@envoy_build_config//:test_extensions_no_autoregister",
    ],
)
