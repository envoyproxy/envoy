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
    name = "matcher_cluster_specifier_test",
    srcs = ["matcher_cluster_specifier_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.matcher"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/router/cluster_specifiers/matcher:config",
        "//source/extensions/router/cluster_specifiers/matcher:matcher_cluster_specifier_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.matcher"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/router/cluster_specifiers/matcher:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.matcher"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/router/cluster_specifiers/matcher:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/matcher/v3:pkg_cc_proto",
    ],
)
