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
    name = "selector_test",
    srcs = ["selector_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:local_address_selector",
        "//test/extensions/dynamic_modules/test_data/c:local_address_selector_missing_select",
    ],
    extension_names = ["envoy.upstream.local_address_selector.dynamic_modules"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/extensions/local_address_selectors/dynamic_modules:config",
        "//source/extensions/local_address_selectors/dynamic_modules:selector_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/local_address_selectors/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:local_address_selector",
    ],
    extension_names = ["envoy.upstream.local_address_selector.dynamic_modules"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "openssl": {"link": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/local_address_selectors/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/local_address_selectors/dynamic_modules/v3:pkg_cc_proto",
    ],
)
