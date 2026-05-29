load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "kafka_stats_sink_impl_test",
    srcs = ["kafka_stats_sink_impl_test.cc"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/test/mesh:kafka_mocks_lib",
        "//contrib/kafka/stat_sinks/source:kafka_stats_sink_impl_lib",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/stat_sinks/source:config_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "@envoy_api//contrib/envoy/extensions/stat_sinks/kafka/v3:pkg_cc_proto",
    ],
)
