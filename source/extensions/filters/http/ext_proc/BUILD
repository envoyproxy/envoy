load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ext_proc",
    srcs = [
        "ext_proc.cc",
        "processor_state.cc",
    ],
    hdrs = [
        "ext_proc.h",
        "processor_state.h",
    ],
    deps = [
        ":client_interface",
        ":mutation_utils_lib",
        "//envoy/event:timer_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_features_lib",
        "//source/extensions/filters/common/mutation_rules:mutation_rules_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/config/common/mutation_rules/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_lib",
        ":ext_proc",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/grpc:status",
        "//envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "mutation_utils_lib",
    srcs = ["mutation_utils.cc"],
    hdrs = ["mutation_utils.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stats:stats_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/mutation_rules:mutation_rules_lib",
        "@com_google_absl//absl/status",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":client_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)
