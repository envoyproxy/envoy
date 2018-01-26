licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "statsd_lib",
    srcs = ["statsd.cc"],
    hdrs = ["statsd.h"],
    external_deps = [
        "envoy_stats",
    ],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/stats:statsd_lib",
        "//source/server:configuration_lib",
    ],
)

envoy_cc_library(
    name = "dog_statsd_lib",
    srcs = ["dog_statsd.cc"],
    hdrs = ["dog_statsd.h"],
    external_deps = [
        "envoy_stats",
    ],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/stats:statsd_lib",
        "//source/server:configuration_lib",
    ],
)

envoy_cc_library(
    name = "metrics_service_lib",
    srcs = ["metrics_service.cc"],
    hdrs = ["metrics_service.h"],
    external_deps = [
        "envoy_stats",
        "envoy_metrics",
    ],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/stats:metrics_service_grpc_lib",
        "//source/server:configuration_lib",
    ],
)
