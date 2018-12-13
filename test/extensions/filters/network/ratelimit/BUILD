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
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    extension_name = "envoy.filters.network.ratelimit",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/tracing:tracing_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.ratelimit",
    deps = [
        "//source/extensions/filters/network/ratelimit:config",
        "//test/mocks/server:server_mocks",
    ],
)
