licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "dynamo_filter_test",
    srcs = ["dynamo_filter_test.cc"],
    extension_name = "envoy.filters.http.dynamo",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/dynamo:dynamo_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dynamo_request_parser_test",
    srcs = ["dynamo_request_parser_test.cc"],
    extension_name = "envoy.filters.http.dynamo",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/http/dynamo:dynamo_request_parser_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dynamo_utility_test",
    srcs = ["dynamo_utility_test.cc"],
    extension_name = "envoy.filters.http.dynamo",
    deps = [
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/dynamo:dynamo_utility_lib",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.dynamo",
    deps = [
        "//source/extensions/filters/http/dynamo:config",
        "//test/mocks/server:server_mocks",
    ],
)
