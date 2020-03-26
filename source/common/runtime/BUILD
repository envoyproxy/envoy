licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "runtime_features_lib",
    srcs = [
        "runtime_features.cc",
    ],
    hdrs = [
        "runtime_features.h",
    ],
    deps = [
        "//source/common/common:hash_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "runtime_lib",
    srcs = [
        "runtime_impl.cc",
    ],
    hdrs = [
        "runtime_impl.h",
        "runtime_protos.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":runtime_features_lib",
        "//include/envoy/config:discovery_service_base_interface",
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
        "//source/common/config:api_version_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v2:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
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
