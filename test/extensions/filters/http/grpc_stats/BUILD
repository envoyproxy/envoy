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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.grpc_stats",
    deps = [
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/grpc_stats:config",
        "//test/common/buffer:utility_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/server:server_mocks",
    ],
)
