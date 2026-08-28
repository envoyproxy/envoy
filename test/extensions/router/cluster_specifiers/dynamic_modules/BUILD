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
    name = "cluster_specifier_test",
    srcs = ["cluster_specifier_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_specifier_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cluster_specifier_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:cluster_specifier_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:cluster_specifier_missing_select",
        "//test/extensions/dynamic_modules/test_data/c:cluster_specifier_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:cluster_specifier_integration_test",
    ],
    extension_names = ["envoy.router.cluster_specifier_plugin.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/retry/host/previous_hosts:config",
        "//source/extensions/router/cluster_specifiers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:cluster_specifier_integration_test",
    ],
    extension_names = ["envoy.router.cluster_specifier_plugin.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/load_balancing_policies/subset:config",
        "//source/extensions/router/cluster_specifiers/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:refresh_route_cluster_filter_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
