licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "message_counter_test",
    srcs = ["message_counter_test.cc"],
    extension_name = "envoy.filters.http.grpc_streaming",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/grpc_streaming:message_counter_lib",
        "//test/common/buffer:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
