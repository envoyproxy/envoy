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

envoy_cc_test(
    name = "backoff_strategy_test",
    srcs = ["backoff_strategy_test.cc"],
    deps = [
        "//source/common/common:backoff_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "assert_test",
    srcs = ["assert_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "base64_test",
    srcs = ["base64_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:base64_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "base64_fuzz_test",
    srcs = ["base64_fuzz_test.cc"],
    corpus = "base64_corpus",
    # Fuzzer is stable, no bugs, simple test target; avoid emitting CO2.
    tags = ["no_fuzz"],
    deps = ["//source/common/common:base64_lib"],
)

envoy_cc_fuzz_test(
    name = "utility_fuzz_test",
    srcs = ["utility_fuzz_test.cc"],
    corpus = "utility_corpus",
    # Fuzzer is stable, no bugs, simple test target; avoid emitting CO2.
    tags = ["no_fuzz"],
    deps = ["//source/common/common:utility_lib"],
)

envoy_cc_fuzz_test(
    name = "hash_fuzz_test",
    srcs = ["hash_fuzz_test.cc"],
    corpus = "hash_corpus",
    deps = ["//source/common/common:hash_lib"],
)

envoy_cc_test(
    name = "cleanup_test",
    srcs = ["cleanup_test.cc"],
    deps = ["//source/common/common:cleanup_lib"],
)

envoy_cc_test(
    name = "mem_block_builder_test",
    srcs = ["mem_block_builder_test.cc"],
    deps = ["//source/common/common:mem_block_builder_lib"],
)

envoy_cc_test(
    name = "phantom_test",
    srcs = ["phantom_test.cc"],
    deps = ["//source/common/common:phantom"],
)

envoy_cc_test(
    name = "fmt_test",
    srcs = ["fmt_test.cc"],
    deps = [
        "//source/common/common:fmt_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_test(
    name = "hash_test",
    srcs = ["hash_test.cc"],
    deps = ["//source/common/common:hash_lib"],
)

envoy_cc_test(
    name = "hex_test",
    srcs = ["hex_test.cc"],
    deps = ["//source/common/common:hex_lib"],
)

envoy_cc_test(
    name = "log_macros_test",
    srcs = ["log_macros_test.cc"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "logger_test",
    srcs = ["logger_test.cc"],
    deps = [
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "logger_fuzz_test",
    srcs = ["logger_fuzz_test.cc"],
    corpus = "logger_corpus",
    # TODO(github.com/envoyproxy/envoy#8893): Re-enable once more fuzz tests are added
    tags = ["no_fuzz"],
    deps = ["//source/common/common:minimal_logger_lib"],
)

envoy_cc_test(
    name = "matchers_test",
    srcs = ["matchers_test.cc"],
    deps = [
        "//source/common/common:matchers_lib",
        "//source/common/config:metadata_lib",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "mutex_tracer_test",
    srcs = ["mutex_tracer_test.cc"],
    deps = [
        "//source/common/common:mutex_tracer_lib",
        "//test/test_common:contention_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//source/common/common:utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "regex_test",
    srcs = ["regex_test.cc"],
    deps = [
        "//source/common/common:regex_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "perf_annotation_test",
    srcs = ["perf_annotation_test.cc"],
    deps = [
        "//source/common/common:perf_annotation_lib",
    ],
)

envoy_cc_test(
    name = "perf_annotation_disabled_test",
    srcs = ["perf_annotation_disabled_test.cc"],
    deps = [
        "//source/common/common:perf_annotation_lib",
    ],
)

envoy_cc_test(
    name = "basic_resource_impl_test",
    srcs = ["basic_resource_impl_test.cc"],
    deps = [
        "//source/common/common:basic_resource_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "token_bucket_impl_test",
    srcs = ["token_bucket_impl_test.cc"],
    deps = [
        "//source/common/common:token_bucket_impl_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "callback_impl_test",
    srcs = ["callback_impl_test.cc"],
    deps = ["//source/common/common:callback_impl_lib"],
)

envoy_cc_benchmark_binary(
    name = "utility_speed_test",
    srcs = ["utility_speed_test.cc"],
    external_deps = [
        "abseil_strings",
        "benchmark",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_benchmark_test(
    name = "utility_speed_test_benchmark_test",
    benchmark_binary = "utility_speed_test",
)

envoy_cc_test(
    name = "lock_guard_test",
    srcs = ["lock_guard_test.cc"],
    deps = [
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_test(
    name = "thread_id_test",
    srcs = ["thread_id_test.cc"],
    external_deps = ["abseil_hash_testing"],
    deps = [
        "//source/common/common:thread_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "thread_test",
    srcs = ["thread_test.cc"],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/common:thread_synchronizer_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "stl_helpers_test",
    srcs = ["stl_helpers_test.cc"],
    deps = [
        "//source/common/common:stl_helpers",
    ],
)

envoy_cc_test(
    name = "version_test",
    srcs = ["version_test.cc"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//source/common/common:version_lib",
    ],
)

envoy_cc_test(
    name = "statusor_test",
    srcs = ["statusor_test.cc"],
    deps = [
        "//source/common/common:statusor_lib",
        "//source/common/http:status_lib",
    ],
)
