licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "http_tracer_impl_proto",
    srcs = ["http_tracer_impl.proto"],
    deps = [
        "@envoy_api//envoy/type/tracing/v2:pkg",
    ],
)

envoy_cc_test(
    name = "http_tracer_impl_test",
    srcs = [
        "http_tracer_impl_test.cc",
    ],
    deps = [
        ":http_tracer_impl_proto_cc_proto",
        "//source/common/common:base64_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/tracing:http_tracer_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
