load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
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

envoy_cc_test(
    name = "json_loader_test",
    srcs = ["json_loader_test.cc"],
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "json_loader_speed_test",
    srcs = ["json_loader_speed_test.cc"],
    external_deps = [
        "benchmark",
        "json",
    ],
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/common/json:rapidjson_loader_lib",
    ],
)

envoy_benchmark_test(
    name = "json_loader_speed_test_benchmark_test",
    benchmark_binary = "json_loader_speed_test",
)
