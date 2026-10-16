load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//source/common/http:sidestream_watermark_lib",
        "//source/extensions/filters/common/ext_proc:grpc_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/network_ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "matching_utils_lib",
    srcs = ["matching_utils.cc"],
    hdrs = ["matching_utils.h"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/local_info:local_info_interface",
        "//source/common/protobuf",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@cel-cpp//eval/public:cel_expr_builder_factory",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@cel-cpp//parser",
            ],
        },
    ),
)

envoy_cc_library(
    name = "ext_proc_lib",
    srcs = ["ext_proc.cc"],
    hdrs = ["ext_proc.h"],
    deps = [
        ":client_lib",
        ":matching_utils_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/network_ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_lib",
        ":matching_utils_lib",
        "//envoy/registry",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/ext_proc:ext_proc_lib",
        "@envoy_api//envoy/extensions/filters/network/ext_proc/v3:pkg_cc_proto",
    ],
)
