load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "extension_config_test",
    srcs = ["extension_config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_extension_new_null",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_config_new",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_config_scheduled",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_constructor",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_extension_destroy",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_extension_new",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_op",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_server_initialized",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_worker_initialized",
    ],
    deps = [
        "//source/extensions/bootstrap/dynamic_modules:extension_config_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_op",
    ],
    deps = [
        "//source/extensions/bootstrap/dynamic_modules:abi_impl",
        "//source/extensions/bootstrap/dynamic_modules:extension_config_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "extension_test",
    srcs = ["extension_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_extension_new_null",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_op",
    ],
    deps = [
        "//source/extensions/bootstrap/dynamic_modules:extension_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_config_new",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_op",
    ],
    deps = [
        "//source/extensions/bootstrap/dynamic_modules:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/dynamic_modules/test_data/c:bootstrap_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:bootstrap_integration_test",
    ],
    deps = [
        "//source/extensions/bootstrap/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
