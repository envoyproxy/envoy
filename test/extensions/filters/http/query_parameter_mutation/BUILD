load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.query_parameter_mutation"],
    deps = [
        "//source/extensions/filters/http/query_parameter_mutation:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/query_parameter_mutation/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.query_parameter_mutation"],
    deps = [
        "//source/extensions/filters/http/query_parameter_mutation:filter_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/query_parameter_mutation/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "query_params_evaluator_test",
    srcs = ["query_params_evaluator_test.cc"],
    deps = [
        "//test/mocks/stream_info:stream_info_mocks",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/query_parameter_mutation:query_params_evaluator_lib",
    ],
)
