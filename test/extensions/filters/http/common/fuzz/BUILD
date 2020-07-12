load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_http_filters",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "filter_fuzz_proto",
    srcs = ["filter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg",
    ],
)

envoy_cc_test_library(
    name = "uber_filter_lib",
    srcs = [
        "uber_filter.cc",
        "uber_per_filter.cc",
    ],
    hdrs = ["uber_filter.h"],
    deps = [
        ":filter_fuzz_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:utility_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
        "//test/proto:bookstore_proto_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "filter_fuzz_test",
    srcs = ["filter_fuzz_test.cc"],
    corpus = "filter_corpus",
    # All Envoy extensions must be linked to the test in order for the fuzzer to pick
    # these up via the NamedHttpFilterConfigFactory.
    deps = [
        ":uber_filter_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//test/config:utility_lib",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v2alpha:pkg_cc_proto",
    ] + envoy_all_http_filters(),
)
