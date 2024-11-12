load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "xray_test",
    srcs = [
        "localized_sampling_test.cc",
        "tracer_test.cc",
        "util_test.cc",
        "xray_tracer_impl_test.cc",
    ],
    extension_names = ["envoy.tracers.xray"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/xray:xray_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.xray"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/xray:config",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "xray_fuzz_test",
    srcs = ["fuzz_test.cc"],
    corpus = "wildcard_matcher_corpus",
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/xray:xray_lib",
    ],
)
