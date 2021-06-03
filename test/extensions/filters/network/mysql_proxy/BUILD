load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "mysql_mock",
    srcs = ["mock.cc"],
    hdrs = ["mock.h"],
    deps = [
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:decoder_interface",
        "//source/extensions/filters/network/mysql_proxy:route_interface",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_terminal_filter_test",
    srcs = [
        "mysql_terminal_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_mock",
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "mysql_test_utils_lib",
    srcs = ["mysql_test_utils.cc"],
    hdrs = ["mysql_test_utils.h"],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        "//source/extensions/filters/network/mysql_proxy:codec_lib",
        "//source/extensions/filters/network/mysql_proxy:util_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_codec_test",
    srcs = [
        "mysql_codec_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_greet_test",
    srcs = [
        "mysql_greet_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_auth_switch_resp_test",
    srcs = [
        "mysql_auth_switch_resp_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_clogin_test",
    srcs = [
        "mysql_clogin_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_clogin_resp_tests",
    srcs = [
        "mysql_clogin_resp_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_filter_test",
    srcs = [
        "mysql_filter_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_mock",
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
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:codec_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_terminal_integration_test",
    srcs = [
        "mysql_terminal_integration_test.cc",
    ],
    data = [
        "mysql_test_terminal_config.yaml",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:codec_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "mysql_route_test",
    srcs = [
        "mysql_route_test.cc",
    ],
    extension_name = "envoy.filters.network.mysql_proxy",
    deps = [
        ":mysql_mock",
        ":mysql_test_utils_lib",
        "//source/extensions/filters/network/mysql_proxy:route_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "mysql_command_test",
    srcs = [
        "mysql_command_test.cc",
    ],
    data = ["mysql_test_config.yaml"],
    extension_name = "envoy.filters.network.mysql_proxy",
    external_deps = ["sqlparser"],
    deps = [
        ":mysql_test_utils_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/mysql_proxy:codec_lib",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)
