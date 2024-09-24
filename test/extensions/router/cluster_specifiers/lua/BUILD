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
    name = "lua_cluster_specifier_test",
    srcs = ["lua_cluster_specifier_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/router/cluster_specifiers/lua:lua_cluster_specifier_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.router.cluster_specifier_plugin.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/router/cluster_specifiers/lua:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
