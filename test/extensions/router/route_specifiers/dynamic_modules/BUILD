load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "route_specifier_test",
    srcs = ["route_specifier_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_missing_on_route",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_no_op",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_override",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_select_template",
        "//test/extensions/dynamic_modules/test_data/c:route_specifier_unknown_decision",
    ],
    extension_names = ["envoy.router.route_specifiers.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/router:config_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/router/route_specifiers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/init:init_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/router/route_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:route_specifier_integration_test",
        "//test/extensions/dynamic_modules/test_data/rust:route_specifier_shadow",
    ],
    extension_names = ["envoy.router.route_specifiers.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/router/route_specifiers/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:clear_route_cache_filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/router/route_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
