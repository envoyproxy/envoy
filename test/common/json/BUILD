load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_fuzz_test(
    name = "json_fuzz_test",
    srcs = ["json_fuzz_test.cc"],
    corpus = "json_corpus",
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//test/fuzz:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

JSON_TEST_DEPS = [
    "//source/common/json:json_loader_lib",
    "//source/common/stats:isolated_store_lib",
    "//test/test_common:utility_lib",
]

envoy_cc_test(
    name = "json_loader_test",
    srcs = ["json_loader_test.cc"],
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "json_sanitizer_test",
    srcs = ["json_sanitizer_test.cc"],
    deps = [
        ":json_sanitizer_test_util_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_internal_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "json_sanitizer_speed_test",
    srcs = ["json_sanitizer_speed_test.cc"],
    deps = [
        "//source/common/json:json_internal_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "json_sanitizer_fuzz_test",
    srcs = ["json_sanitizer_fuzz_test.cc"],
    corpus = "json_sanitizer_corpus",
    deps = [
        ":json_sanitizer_test_util_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/protobuf:utility_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test(
    name = "json_streamer_test",
    srcs = ["json_streamer_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_internal_lib",
        "//source/common/json:json_streamer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_test_binary(
    name = "gen_excluded_unicodes",
    srcs = ["gen_excluded_unicodes.cc"],
    deps = [
        ":utf8_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "json_sanitizer_test_util_lib",
    srcs = [
        "json_sanitizer_test_util.cc",
    ],
    hdrs = ["json_sanitizer_test_util.h"],
    deps = [
        ":utf8_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "utf8_lib",
    srcs = ["utf8.cc"],
    hdrs = ["utf8.h"],
)

envoy_cc_test(
    name = "utf8_test",
    srcs = ["utf8_test.cc"],
    deps = [":utf8_lib"],
)
