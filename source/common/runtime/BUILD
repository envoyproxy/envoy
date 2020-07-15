load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

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
        # AVOID ADDING TO THESE DEPENDENCIES IF POSSIBLE
        # Any code using runtime guards depends on this library, and the more dependencies there are,
        # the harder it is to runtime-guard without dependency loops.
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:hash_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_library(
    name = "runtime_protos_lib",
    hdrs = [
        "runtime_protos.h",
    ],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "runtime_lib",
    srcs = [
        "runtime_impl.cc",
    ],
    hdrs = [
        "runtime_impl.h",
    ],
    deps = [
        ":runtime_features_lib",
        ":runtime_protos_lib",
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
        "//source/common/common:random_generator_lib",  #FIXME
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/filesystem:directory_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
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
