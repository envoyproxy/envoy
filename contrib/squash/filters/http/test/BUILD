load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "squash_filter_test",
    srcs = ["squash_filter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/squash/filters/http/source:squash_filter_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "squash_filter_integration_test",
    srcs = ["squash_filter_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/squash/filters/http/source:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/squash/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)
