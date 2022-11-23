load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "config_discovery_lib",
    srcs = ["config_discovery_impl.cc"],
    hdrs = ["config_discovery_impl.h"],
    deps = [
        "//envoy/config:subscription_interface",
        "//envoy/filter:config_provider_manager_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:containers_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
