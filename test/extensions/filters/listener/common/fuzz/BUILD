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
    srcs = ["listener_filter_fuzzer.cc"],
    hdrs = ["listener_filter_fuzzer.h"],
    rbe_pool = "2core",
    deps = [
        ":listener_filter_fakes",
        ":listener_filter_fuzzer_proto_cc_proto",
        "//envoy/network:filter_interface",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:connection_balancer_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test_library(
    name = "listener_filter_fakes",
    srcs = ["listener_filter_fakes.cc"],
    hdrs = ["listener_filter_fakes.h"],
    rbe_pool = "2core",
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//test/mocks/network:network_mocks",
    ],
)
