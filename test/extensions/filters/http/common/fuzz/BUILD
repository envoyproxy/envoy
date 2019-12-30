licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test_library(
    name = "decoder_filter_fuzz_lib",
    hdrs = ["decoder_filter_fuzz.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//test/fuzz:utility_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_proto_library(
    name = "filter_fuzz_proto",
    srcs = ["filter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "filter_fuzz_test",
    srcs = ["filter_fuzz_test.cc"],
    corpus = "filter_corpus",
    deps = [
        ":decoder_filter_fuzz_lib",
        ":filter_fuzz_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/buffer:config",
        "//test/config:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/buffer/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
    ],
)
