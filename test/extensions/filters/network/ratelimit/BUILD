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
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    extension_names = [
        "envoy.filters.network.ratelimit",
        "envoy.filters.network.tcp_proxy",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "@envoy_api//envoy/extensions/filters/network/ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/ratelimit:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/ratelimit/v3:pkg_cc_proto",
    ],
)
