load(
    "//bazel:envoy_build_system.bzl",
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
    hdrs = ["listener_filter_fuzzer.h"],
    deps = [
        "//source/common/common:hex_lib",
        ":listener_filter_fuzzer_proto_cc_proto",
        ":listener_filter_fakes",
        "//include/envoy/network:filter_interface",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test_library(
    name = "listener_filter_fakes",
    hdrs = ["listener_filter_fakes.h"],
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
    ],
)
