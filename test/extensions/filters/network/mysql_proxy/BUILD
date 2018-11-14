licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_cc_test_library(
    name = "mysql_test_utils_lib",
    srcs = ["mysql_test_utils.cc"],
    hdrs = ["mysql_test_utils.h"],
    deps = [
        "//source/extensions/filters/network/mysql_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_codec_tests",
    srcs = [
        "mysql_codec_test.cc",
    ],
    data = [
        "mysql_test_config.yaml",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_filter_tests",
    srcs = [
        "mysql_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "mysql_integration_test",
    srcs = [
        "mysql_integration_test.cc",
    ],
    data = [
        "mysql_test_config.yaml",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
