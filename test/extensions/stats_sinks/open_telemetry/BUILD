load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "open_telemetry_impl_test",
    srcs = ["open_telemetry_impl_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    deps = [
        "//source/common/stats:histogram_lib",
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/server:instance_mocks",
    ],
)

envoy_extension_cc_test(
    name = "open_telemetry_http_impl_test",
    srcs = ["open_telemetry_http_impl_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_http_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "open_telemetry_integration_test",
    srcs = ["open_telemetry_integration_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "open_telemetry_benchmark",
    srcs = [
        "open_telemetry_benchmark.cc",
    ],
    deps = [
        "//envoy/stats:tag_interface",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:inlined_vector",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "open_telemetry_benchmark_test",
    benchmark_binary = "open_telemetry_benchmark",
)
