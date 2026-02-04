load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "metadata_object_lib",
    srcs = ["metadata_object.cc"],
    hdrs = ["metadata_object.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/common:hashable_interface",
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:hash_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "workload_discovery_lib",
    srcs = ["workload_discovery.cc"],
    hdrs = ["workload_discovery.h"],
    deps = [
        ":metadata_object_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:non_copyable",
        "//source/common/config:subscription_base_interface",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "@envoy_api//contrib/envoy/extensions/filters/common/workload_discovery/v3:pkg_cc_proto",
    ],
)
