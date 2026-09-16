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
    name = "priority_group_cluster_specifier_test",
    srcs = ["priority_group_cluster_specifier_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.priority_group"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/router/cluster_specifiers/priority_group:config",
        "//source/extensions/router/cluster_specifiers/priority_group:priority_group_cluster_specifier_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = [
        "envoy.router.cluster_specifier_plugin.priority_group",
        "envoy.filters.http.lua",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/lua:config",
        "//source/extensions/router/cluster_specifiers/priority_group:config",
        "//test/integration:autonomous_upstream_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/priority_group/v3:pkg_cc_proto",
    ],
)
