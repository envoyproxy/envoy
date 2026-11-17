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
    name = "connection_limit_test",
    srcs = ["connection_limit_test.cc"],
    extension_names = ["envoy.filters.network.connection_limit"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/network/connection_limit:connection_limit_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/extensions/filters/network/connection_limit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "connection_limit_integration_test",
    size = "large",
    srcs = ["connection_limit_integration_test.cc"],
    extension_names = ["envoy.filters.network.connection_limit"],
    # linux_x64_xlarge: the coverage link peaks at 14851MB,
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
        "//source/extensions/filters/network/connection_limit:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
