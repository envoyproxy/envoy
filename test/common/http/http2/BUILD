licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "codec_impl_fuzz_proto",
    srcs = ["codec_impl_fuzz.proto"],
    external_deps = ["well_known_protos"],
)

envoy_cc_fuzz_test(
    name = "codec_impl_fuzz_test",
    srcs = ["codec_impl_fuzz_test.cc"],
    corpus = "codec_impl_corpus",
    deps = [
        ":codec_impl_fuzz_proto",
        "//source/common/http:header_map_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/stats:stats_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "conn_pool_test",
    srcs = ["conn_pool_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
