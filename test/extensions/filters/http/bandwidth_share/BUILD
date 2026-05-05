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

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = [
        "envoy.filters.http.bandwidth_share",
        "envoy.formatter.cel",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/bandwidth_share:config",
        "//source/extensions/formatter/cel:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_test(
    name = "fair_token_bucket_impl_test",
    srcs = ["fair_token_bucket_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/bandwidth_share:fair_token_bucket_impl_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    srcs = ["filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.bandwidth_share"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/bandwidth_share:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_share/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
