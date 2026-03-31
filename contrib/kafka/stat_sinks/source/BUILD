load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "kafka_stats_sink_impl_lib",
    srcs = ["kafka_stats_sink_impl.cc"],
    hdrs = ["kafka_stats_sink_impl.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//bazel/foreign_cc:librdkafka",
        "//envoy/stats:sink_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/json:json_streamer_lib",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":kafka_stats_sink_impl_lib",
        "//contrib/kafka/filters/network/source/mesh:librdkafka_utils_impl_lib",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/server:configuration_lib",
        "@envoy_api//contrib/envoy/extensions/stat_sinks/kafka/v3:pkg_cc_proto",
    ],
)
