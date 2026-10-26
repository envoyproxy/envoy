load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_echo",
    ],
    extension_names = ["envoy.matching.inputs.dynamic_module_string_data_input"],
    deps = [
        "//source/extensions/matching/http/dynamic_modules:string_data_input_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/matching/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "data_input_test",
    srcs = ["data_input_test.cc"],
    extension_names = ["envoy.matching.inputs.dynamic_module_data_input"],
    deps = [
        "//envoy/registry",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/network:address_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/matching/http/dynamic_modules:data_input_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "string_data_input_test",
    srcs = ["string_data_input_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_echo",
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:matcher_string_input_missing_get",
    ],
    extension_names = ["envoy.matching.inputs.dynamic_module_string_data_input"],
    deps = [
        "//envoy/registry",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/network:address_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/matching/http/dynamic_modules:string_data_input_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)
