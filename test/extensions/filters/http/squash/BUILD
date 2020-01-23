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
    name = "squash_filter_test",
    srcs = ["squash_filter_test.cc"],
    extension_name = "envoy.filters.http.squash",
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/squash:squash_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "squash_filter_integration_test",
    srcs = ["squash_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.squash",
    deps = [
        "//source/extensions/filters/http/squash:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.squash",
    deps = [
        "//source/extensions/filters/http/squash:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)
