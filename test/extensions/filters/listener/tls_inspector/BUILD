licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "tls_inspector_test",
    srcs = ["tls_inspector_test.cc"],
    deps = [
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:tls_utility_lib",
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
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:tls_utility_lib",
    ],
)
