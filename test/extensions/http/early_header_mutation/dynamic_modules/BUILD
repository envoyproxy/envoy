load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_dyn_module_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_dyn_module_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_mutate",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_no_op",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_rewrite",
    ],
    deps = [
        "//envoy/registry",
        "//source/extensions/http/early_header_mutation/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_dyn_module_test(
    name = "early_header_mutation_test",
    srcs = ["early_header_mutation_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_missing_mutate",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_no_op",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_rewrite",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_stop_chain",
    ],
    deps = [
        "//source/extensions/http/early_header_mutation/dynamic_modules:early_header_mutation_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

# Calls the extern "C" callbacks directly against a stack-allocated context, so no module is
# loaded and the dyn-module link options are not needed.
envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/http/early_header_mutation/dynamic_modules:early_header_mutation_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_rewrite",
        "//test/extensions/dynamic_modules/test_data/c:early_header_mutation_stop_chain",
        "//test/extensions/dynamic_modules/test_data/cpp:early_header_mutation_integration_test",
        "//test/extensions/dynamic_modules/test_data/go:early_header_mutation_integration_test",
        "//test/extensions/dynamic_modules/test_data/rust:early_header_mutation_integration_test",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        # The C++ SDK compiles every extension point into one archive, so the test binary must
        # export the callbacks of the other dynamic module extensions the SDK references.
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/filters/http/dynamic_modules:abi_impl",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/network/dynamic_modules:config",
        "//source/extensions/http/early_header_mutation/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/dynamic_modules/v3:pkg_cc_proto",
    ],
)
