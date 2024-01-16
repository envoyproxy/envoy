load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_swift_test(
    name = "quic_stream_test",
    srcs = [
        "QUICStreamTest.swift",
    ],
    data = [
        "@envoy//test/config/integration/certs",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
