load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//test/extensions:extensions_build_system.bzl", "envoy_extension_cc_dyn_module_test")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "route_extension_test",
    srcs = ["route_extension_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:route_extension_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:route_extension_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:route_extension_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:route_extension_missing_on_route",
        "//test/extensions/dynamic_modules/test_data/c:route_extension_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:route_extension_integration_test",
    ],
    extension_names = ["envoy.router.route_extension.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/router/route_extension/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/router/route_extension/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:route_extension_integration_test",
    ],
    extension_names = ["envoy.router.route_extension.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/router/route_extension/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/router/route_extension/dynamic_modules/v3:pkg_cc_proto",
    ],
)
