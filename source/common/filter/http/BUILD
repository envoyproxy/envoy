load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "filter_config_discovery_lib",
    srcs = ["filter_config_discovery_impl.cc"],
    hdrs = ["filter_config_discovery_impl.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/filter/http:filter_config_provider_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
