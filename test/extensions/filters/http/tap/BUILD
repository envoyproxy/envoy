load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
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
    srcs = ["tap_filter_test.cc"],
    extension_name = "envoy.filters.http.tap",
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
    srcs = ["tap_config_impl_test.cc"],
    extension_name = "envoy.filters.http.tap",
    deps = [
        ":common",
        "//source/extensions/filters/http/tap:tap_config_impl",
        "//test/extensions/common/tap:common",
        "//test/mocks:common_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "tap_filter_integration_test",
    srcs = ["tap_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.tap",
    deps = [
        "//source/extensions/filters/http/tap:config",
        "//test/extensions/common/tap:common",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)
