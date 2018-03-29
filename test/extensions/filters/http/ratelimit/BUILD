licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/config:filter_json_lib",
        "//source/common/http:headers_lib",
        "//source/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/http/ratelimit:ratelimit_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/http/ratelimit:config",
        "//test/mocks/server:server_mocks",
    ],
)
