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
    name = "req_without_query_test",
    srcs = ["req_without_query_test.cc"],
    extension_names = ["envoy.formatter.req_without_query"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/formatter/req_without_query:config",
        "//source/extensions/formatter/req_without_query:req_without_query_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
