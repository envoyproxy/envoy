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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.header_weighted"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/router/cluster_specifiers/header_weighted:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_weighted_test",
    srcs = ["header_weighted_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.header_weighted"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/router/cluster_specifiers/header_weighted:config",
        "//source/extensions/router/cluster_specifiers/header_weighted:header_weighted_cluster_specifier_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/header_weighted/v3:pkg_cc_proto",
    ],
)
