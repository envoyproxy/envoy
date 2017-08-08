licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "dynamo_filter_test",
    srcs = ["dynamo_filter_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/dynamo:dynamo_filter_lib",
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
        "//source/common/dynamo:dynamo_request_parser_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dynamo_utility_test",
    srcs = ["dynamo_utility_test.cc"],
    deps = [
        "//source/common/dynamo:dynamo_utility_lib",
        "//source/common/stats:stats_lib",
    ],
)
