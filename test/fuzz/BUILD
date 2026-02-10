load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    "@rules_fuzzing//fuzzing:cc_defs.bzl",
    "cc_fuzzing_engine",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_signal_trace",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

exports_files(["headers.dict"])

cc_library(
    name = "fuzzed_data_provider",
    hdrs = ["@llvm_toolchain_llvm//:lib/clang/18/include/fuzzer/FuzzedDataProvider.h"],
    include_prefix = "",
    strip_include_prefix = "/lib/clang/18/include",
    visibility = ["//visibility:public"],
)

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":fuzz_runner_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stats:isolated_store_lib",
        "//test:test_listener_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/synchronization",
    ] + envoy_select_signal_trace(["//source/common/signal:sigaction_lib"]),
)

envoy_cc_test_library(
    name = "fuzz_runner_lib",
    srcs = ["fuzz_runner.cc"],
    hdrs = ["fuzz_runner.h"],
    deps = [
        ":fuzzed_data_provider",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/event:libevent_lib",
        "//source/common/http/http2:codec_lib",
        "//source/exe:process_wide_lib",
        "//test/test_common:environment_lib",
        "@libprotobuf-mutator//:libprotobuf_mutator",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    rbe_pool = "6gig",
    deps = [
        ":common_proto_cc_proto",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib_header",
        "//test/common/stream_info:test_util",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@quiche//:http2_adapter",
    ],
)

envoy_cc_test_library(
    name = "mutable_visitor_lib",
    srcs = ["mutable_visitor.cc"],
    hdrs = ["mutable_visitor.h"],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib_header",
        "//source/common/protobuf:visitor_lib",
        "@abseil-cpp//absl/cleanup",
        "@xds//udpa/type/v1:pkg_cc_proto",
        "@xds//xds/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "validated_input_generator_lib",
    srcs = ["validated_input_generator.cc"],
    hdrs = ["validated_input_generator.h"],
    deps = [
        "//test/fuzz:mutable_visitor_lib",
        "//test/fuzz:random_lib",
        "@com_envoyproxy_protoc_gen_validate//validate:cc_validate",
        "@libprotobuf-mutator//:libprotobuf_mutator",
        "@xds//udpa/type/v1:pkg_cc_proto",
        "@xds//xds/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "random_lib",
    hdrs = ["random.h"],
    deps = [
        "//envoy/common:random_generator_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_test(
    name = "random_test",
    srcs = ["random_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//test/fuzz:random_lib",
    ],
)

cc_fuzzing_engine(
    name = "fuzz_runner_engine",
    testonly = True,
    display_name = "Fuzz Test Runner",
    launcher = "fuzz_runner_launcher.sh",
    library = ":main",
)
