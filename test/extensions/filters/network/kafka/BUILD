licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "kafka_request_lib_test",
    srcs = ["code_test.cc"],
    extension_name = "envoy.filters.network.kafka",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_request_lib",
        "//test/mocks/server:server_mocks",
    ],
)
