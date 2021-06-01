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
    name = "listener_filter_fuzzer_proto",
    srcs = ["listener_filter_fuzzer.proto"],
)

envoy_cc_test_library(
    name = "listener_filter_fuzzer_lib",
    srcs = ["listener_filter_fuzzer.cc"],
    hdrs = ["listener_filter_fuzzer.h"],
    deps = [
        ":listener_filter_fakes",
        ":listener_filter_fuzzer_proto_cc_proto",
        "//include/envoy/network:filter_interface",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test_library(
    name = "listener_filter_fakes",
    srcs = ["listener_filter_fakes.cc"],
    hdrs = ["listener_filter_fakes.h"],
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "fuzzed_input_test",
    srcs = ["fuzzed_input_test.cc"],
    deps = [
        ":listener_filter_fuzzer_lib",
    ],
)
