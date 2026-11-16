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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.grpc_stats"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
    },
    deps = [
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/grpc_stats:config",
        "//test/common/buffer:utility_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_stats/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "grpc_stats_filter_integration_test",
    size = "large",
    srcs = ["grpc_stats_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.grpc_stats"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "openssl": {"link": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/grpc_stats:config",
        "//test/integration:http_integration_lib",
    ],
)
