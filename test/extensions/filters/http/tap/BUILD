load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_admin_functionality",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "common",
    hdrs = ["common.h"],
    deps = [
        "//source/extensions/filters/http/tap:tap_config_interface",
    ],
)

envoy_extension_cc_test(
    name = "tap_filter_test",
    srcs = envoy_select_admin_functionality(["tap_filter_test.cc"]),
    extension_names = ["envoy.filters.http.tap"],
    rbe_pool = "2core",
    deps = [
        ":common",
        "//source/extensions/filters/http/tap:config",
        "//source/extensions/filters/http/tap:tap_config_interface",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "tap_config_impl_test",
    srcs = envoy_select_admin_functionality(["tap_config_impl_test.cc"]),
    extension_names = ["envoy.filters.http.tap"],
    rbe_pool = "2core",
    deps = [
        ":common",
        "//source/extensions/filters/http/tap:tap_config_impl",
        "//test/extensions/common/tap:common",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "tap_filter_integration_test",
    size = "large",
    srcs = envoy_select_admin_functionality(["tap_filter_integration_test.cc"]),
    extension_names = ["envoy.filters.http.tap"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/tap:config",
        "//test/extensions/common/tap:common",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)
