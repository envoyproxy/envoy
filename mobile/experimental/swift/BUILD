load("@envoy_mobile//bazel:swift_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "quic_stream_test",
    srcs = [
        "QUICStreamTest.swift",
    ],
    data = [
        "@envoy//test/config/integration/certs",
    ],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
