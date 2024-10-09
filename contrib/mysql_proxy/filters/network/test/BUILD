load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test_library(
    name = "mysql_test_utils_lib",
    srcs = ["mysql_test_utils.cc"],
    hdrs = ["mysql_test_utils.h"],
    deps = [
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_codec_tests",
    srcs = [
        "mysql_codec_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_greet_tests",
    srcs = [
        "mysql_greet_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_auth_switch_resp_tests",
    srcs = [
        "mysql_auth_switch_resp_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_clogin_tests",
    srcs = [
        "mysql_clogin_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_clogin_resp_tests",
    srcs = [
        "mysql_clogin_resp_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
    ],
)

envoy_cc_test(
    name = "mysql_filter_tests",
    srcs = [
        "mysql_filter_test.cc",
    ],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:config",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "mysql_integration_test",
    srcs = [
        "mysql_integration_test.cc",
    ],
    data = [
        "mysql_test_config.yaml",
    ],
    rbe_pool = "2core",
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:config",
        "//contrib/mysql_proxy/filters/network/source:filter_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_cc_test(
    name = "mysql_command_tests",
    srcs = [
        "mysql_command_test.cc",
    ],
    data = ["mysql_test_config.yaml"],
    deps = [
        ":mysql_test_utils_lib",
        "//contrib/mysql_proxy/filters/network/source:codec_lib",
        "//contrib/mysql_proxy/filters/network/source:config",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "@com_github_envoyproxy_sqlparser//:sqlparser",
    ],
)
