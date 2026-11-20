load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cluster_writer_lib",
    srcs = ["cluster_writer.cc"],
    hdrs = ["cluster_writer.h"],
    deps = [
        ":resource_writer_lib",
        "//envoy/init:manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:fmt_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/init:target_lib",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_writer_lib",
    srcs = ["listener_writer.cc"],
    hdrs = ["listener_writer.h"],
    deps = [
        ":resource_writer_lib",
        "//envoy/event:schedulable_cb_interface",
        "//envoy/init:manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:fmt_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_writer_lib",
    srcs = ["resource_writer.cc"],
    hdrs = ["resource_writer.h"],
    deps = [
        ":writer_shared_state_lib",
        "//envoy/common:exception_lib",
        "//envoy/common:pure_lib",
        "//envoy/common:time_interface",
        "//envoy/event:schedulable_cb_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "writer_shared_state_lib",
    srcs = ["writer_shared_state.cc"],
    hdrs = ["writer_shared_state.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:xds_manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/singleton:manager_interface",
        "//source/common/config:dependent_type_urls_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "extension_config_lib",
    srcs = ["extension_config.cc"],
    hdrs = ["extension_config.h"],
    deps = [
        ":cluster_writer_lib",
        ":listener_writer_lib",
        "//envoy/common:optref_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:async_client_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:options_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/http:message_lib",
        "//source/common/init:target_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:metric_registry",
    ],
)

envoy_cc_library(
    name = "extension_lib",
    srcs = ["extension.cc"],
    hdrs = ["extension.h"],
    deps = [
        ":extension_config_lib",
        "//envoy/common:callback",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":extension_config_lib",
        ":extension_lib",
        "//envoy/server:admin_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":abi_impl",
        ":extension_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/bootstrap/dynamic_modules/v3:pkg_cc_proto",
    ],
)
