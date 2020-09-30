load("@envoy_mobile//bazel:swift_test.bzl", "envoy_mobile_swift_test")

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
