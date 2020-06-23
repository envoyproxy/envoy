load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test_library(
    name = "postgres_test_utils_lib",
    srcs = ["postgres_test_utils.cc"],
    hdrs = ["postgres_test_utils.h"],
    extension_name = "envoy.filters.network.postgres_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_extension_cc_test(
    name = "postgres_decoder_tests",
    srcs = [
        "postgres_decoder_test.cc",
    ],
    extension_name = "envoy.filters.network.postgres_proxy",
    deps = [
        ":postgres_test_utils_lib",
        "//source/extensions/filters/network/postgres_proxy:filter",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "postgres_filter_tests",
    srcs = [
        "postgres_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.postgres_proxy",
    deps = [
        ":postgres_test_utils_lib",
        "//source/extensions/filters/network/postgres_proxy:filter",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "postgres_integration_test",
    srcs = [
        "postgres_integration_test.cc",
    ],
    data = [
        "postgres_test_config.yaml",
    ],
    extension_name = "envoy.filters.network.postgres_proxy",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/postgres_proxy:config",
        "//source/extensions/filters/network/postgres_proxy:filter",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
