licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    deps = ["@envoy_api//envoy/api/v2/core:pkg"],
)

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":fuzz_runner_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "fuzz_runner_lib",
    srcs = ["fuzz_runner.cc"],
    hdrs = ["fuzz_runner.h"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/event:libevent_lib",
        "//source/common/http/http2:codec_lib",
        "//source/exe:process_wide_lib",
        "//test/test_common:environment_lib",
        "@com_github_google_libprotobuf_mutator//:libprotobuf_mutator",
        "@org_llvm_releases_compiler_rt//:fuzzed_data_provider",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        ":common_proto_cc_proto",
        "//source/common/common:empty_string",
        "//source/common/network:utility_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)
