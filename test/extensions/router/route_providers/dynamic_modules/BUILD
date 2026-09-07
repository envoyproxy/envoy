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
    name = "route_provider_test",
    srcs = ["route_provider_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:route_provider_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:route_provider_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:route_provider_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:route_provider_missing_select",
        "//test/extensions/dynamic_modules/test_data/c:route_provider_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:route_provider_integration_test",
    ],
    extension_names = ["envoy.router.route_provider.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//source/extensions/retry/host/previous_hosts:config",
        "//source/extensions/router/route_providers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/router/route_providers/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:route_provider_integration_test",
    ],
    extension_names = ["envoy.router.route_provider.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/header_mutation:config",
        "//source/extensions/formatter/metadata:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//source/extensions/router/route_providers/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/router/route_providers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
