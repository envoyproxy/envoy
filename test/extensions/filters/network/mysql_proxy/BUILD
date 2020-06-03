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
    name = "mysql_test_utils_lib",
    srcs = ["mysql_test_utils.cc"],
    hdrs = ["mysql_test_utils.h"],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        "//source/extensions/filters/network/mysql_proxy:proxy_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_codec_tests",
    srcs = [
        "mysql_codec_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:proxy_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_filter_tests",
    srcs = [
        "mysql_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    tags = ["fails_on_windows"],
    deps = [
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//test/mocks/network:network_mocks",
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
    tags = ["fails_on_windows"],
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/mysql_proxy:proxy_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_command_tests",
    srcs = [
        "mysql_command_test.cc",
    ],
    data = ["mysql_test_config.yaml"],
    extension_name = "envoy.filters.network.mysql_proxy",
    external_deps = ["sqlparser"],
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/mysql_proxy:proxy_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
