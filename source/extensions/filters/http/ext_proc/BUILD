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
    srcs = ["ext_proc.cc"],
    hdrs = ["ext_proc.h"],
    deps = [
        ":client_interface",
        ":mutation_utils_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/stats:stats_macros",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        ":client_lib",
        ":ext_proc",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
        "//include/envoy/grpc:status",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "mutation_utils_lib",
    srcs = ["mutation_utils.cc"],
    hdrs = ["mutation_utils.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":client_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)
