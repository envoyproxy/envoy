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
    name = "body_size_limit_filter_test",
    srcs = ["body_size_limit_filter_test.cc"],
    extension_names = ["envoy.filters.http.body_size_limit"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/body_size_limit:body_size_limit_filter_lib",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/extensions/filters/http/body_size_limit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "body_size_limit_filter_integration_test",
    size = "large",
    srcs = ["body_size_limit_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.body_size_limit"],
    rbe_pool = "6gig",
    shard_count = 4,
    deps = [
        "//source/extensions/filters/http/body_size_limit:config",
        "//source/extensions/filters/http/filter_chain:config",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/body_size_limit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/filter_chain/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.body_size_limit"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/body_size_limit:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/body_size_limit/v3:pkg_cc_proto",
    ],
)
