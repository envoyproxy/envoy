licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "tls_inspector_test",
    srcs = ["tls_inspector_test.cc"],
    deps = [
        ":tls_utility_lib",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_binary(
    name = "tls_inspector_benchmark",
    testonly = 1,
    srcs = ["tls_inspector_benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":tls_utility_lib",
        "//source/common/network:listen_socket_lib",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_library(
    name = "tls_utility_lib",
    srcs = ["tls_utility.cc"],
    hdrs = ["tls_utility.h"],
    external_deps = ["ssl"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)
