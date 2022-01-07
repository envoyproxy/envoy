load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
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
        "//envoy/runtime:runtime_interface",
        "//source/common/common:hash_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "runtime_protos_lib",
    hdrs = [
        "runtime_protos.h",
    ],
    deps = [
        "//envoy/runtime:runtime_interface",
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
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/init:manager_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
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
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic/platform:quiche_flags_impl_lib",
    ]),
)
