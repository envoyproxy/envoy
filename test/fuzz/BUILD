load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

exports_files(["headers.dict"])

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    external_deps = [
        "abseil_symbolize",
    ],
    deps = [
        ":fuzz_runner_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": ["//source/common/signal:sigaction_lib"],
    }),
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
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "random_lib",
    hdrs = ["random.h"],
    deps = [
        "//include/envoy/common:random_generator_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_test(
    name = "random_test",
    srcs = ["random_test.cc"],
    deps = [
        "//test/fuzz:random_lib",
    ],
)
