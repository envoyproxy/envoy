load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/network/tcp_bandwidth_limit",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/network/tcp_bandwidth_limit:config",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "tcp_bandwidth_limit_integration_test",
    size = "large",
    srcs = ["tcp_bandwidth_limit_integration_test.cc"],
    extension_names = ["envoy.filters.network.tcp_bandwidth_limit"],
    # linux_x64_xlarge: the coverage link peaks at 15507MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {
            "build": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "openssl": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
            "test": "linux_x64_medium",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
    },
    deps = [
        "//source/extensions/filters/network/tcp_bandwidth_limit:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
