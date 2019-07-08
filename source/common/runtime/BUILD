licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "runtime_lib",
    srcs = [
        "runtime_features.cc",
        "runtime_impl.cc",
    ],
    hdrs = [
        "runtime_features.h",
        "runtime_impl.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
        "@envoy_api//envoy/service/discovery/v2:rtds_cc",
    ],
)

envoy_cc_library(
    name = "uuid_util_lib",
    srcs = ["uuid_util.cc"],
    hdrs = ["uuid_util.h"],
    deps = [
        ":runtime_lib",
        "//source/common/common:utility_lib",
    ],
)
