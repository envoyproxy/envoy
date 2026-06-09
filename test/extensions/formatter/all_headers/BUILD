load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "all_headers_test",
    srcs = ["all_headers_test.cc"],
    extension_names = ["envoy.formatter.all_headers"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/formatter/all_headers:all_headers_lib",
        "//source/extensions/formatter/all_headers:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)
