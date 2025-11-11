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
    name = "stats_test",
    srcs = ["stats_test.cc"],
    extension_names = ["envoy.access_loggers.stats"],
    deps = [
        "//source/common/formatter:stream_info_formatter_extension_lib",
        "//source/extensions/access_loggers/stats:stats_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.access_loggers.stats"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/access_loggers/stats:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
