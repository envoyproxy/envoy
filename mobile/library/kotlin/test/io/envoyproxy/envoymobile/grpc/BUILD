licenses(["notice"])  # Apache 2

load("//bazel:kotlin_test.bzl", "envoy_mobile_kt_test")

envoy_mobile_kt_test(
    name = "darwin_grpc_codec_test",
    srcs = [
        "GrpcTest.kt",
    ],
    deps = [
        "//library/kotlin/test/grpc:envoy_mobile_grpc_java_test_server_lib",
    ],
)
