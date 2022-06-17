load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "dynamo_filter_test",
    srcs = ["dynamo_filter_test.cc"],
    deps = [
        "//contrib/dynamo/filters/http/source:dynamo_filter_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dynamo_request_parser_test",
    srcs = ["dynamo_request_parser_test.cc"],
    deps = [
        "//contrib/dynamo/filters/http/source:dynamo_request_parser_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dynamo_stats_test",
    srcs = ["dynamo_stats_test.cc"],
    deps = [
        "//contrib/dynamo/filters/http/source:dynamo_stats_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/dynamo/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//contrib/envoy/extensions/filters/http/dynamo/v3:pkg_cc_proto",
    ],
)
