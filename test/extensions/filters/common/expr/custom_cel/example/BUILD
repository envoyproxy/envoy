load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "custom_cel_variables_test",
    srcs = ["custom_cel_variables_test.cc"],
    deps = [
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "//source/extensions/filters/common/expr/custom_cel/example:example_custom_cel_vocabulary_lib",
        "//test/config:utility_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_function",
        "@com_google_cel_cpp//eval/public:cel_function_adapter",
        "@com_google_cel_cpp//eval/public:cel_function_registry",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public/containers:container_backed_map_impl",
    ],
)

envoy_cc_test(
    name = "custom_cel_functions_test",
    srcs = ["custom_cel_functions_test.cc"],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "//source/extensions/filters/common/expr/custom_cel/example:example_custom_cel_vocabulary_lib",
        "//test/config:utility_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_function_registry",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_test(
    name = "example_custom_cel_vocabulary_test",
    srcs = ["example_custom_cel_vocabulary_test.cc"],
    deps = [
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "//source/extensions/filters/common/expr/custom_cel/example:custom_cel_variables_lib",
        "//source/extensions/filters/common/expr/custom_cel/example:example_custom_cel_vocabulary_lib",
        "//test/config:utility_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_function_adapter",
        "@com_google_cel_cpp//eval/public:cel_function_registry",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/expr/custom_cel_vocabulary/example/v3:pkg_cc_proto",
    ],
)
