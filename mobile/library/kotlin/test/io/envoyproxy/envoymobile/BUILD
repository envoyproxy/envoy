licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:kotlin_test.bzl", "envoy_mobile_kt_test")

envoy_package()

envoy_mobile_kt_test(
    name = "request_builder_test",
    srcs = [
        "RequestBuilderTest.kt",
    ],
    deps = [
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_test(
    name = "request_test",
    srcs = [
        "RequestTest.kt",
    ],
    deps = [
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
