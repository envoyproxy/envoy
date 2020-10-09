load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

# NB: Used to break the circular dependency between wasm_lib and null_plugin_lib.
envoy_cc_library(
    name = "wasm_hdr",
    hdrs = [
        "context.h",
        "wasm.h",
        "wasm_extension.h",
        "wasm_state.h",
        "wasm_vm.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":well_known_names",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/server:lifecycle_notifier_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:datasource_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stats:stats_lib",
        "//source/common/version:version_includes",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/http:well_known_names",
        "@com_google_cel_cpp//eval/public:activation",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
        "@proxy_wasm_cpp_host//:include",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
)

envoy_cc_library(
    name = "wasm_interoperation_lib",
    srcs = [
        "wasm_state.cc",
    ],
    hdrs = [
        "wasm_state.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@com_github_google_flatbuffers//:flatbuffers",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//tools:flatbuffers_backed_impl",
    ],
)

envoy_cc_library(
    name = "wasm_lib",
    srcs = [
        "context.cc",
        "foreign.cc",
        "wasm.cc",
        "wasm_extension.cc",
        "wasm_vm.cc",
    ],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DWASM_USE_CEL_PARSER",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":wasm_hdr",
        ":wasm_interoperation_lib",
        "//external:abseil_base",
        "//external:abseil_node_hash_map",
        "//include/envoy/server:lifecycle_notifier_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/common/wasm/ext:declare_property_cc_proto",
        "//source/extensions/common/wasm/ext:envoy_null_vm_wasm_api",
        "//source/extensions/filters/common/expr:context_lib",
        "@com_google_cel_cpp//eval/eval:field_access",
        "@com_google_cel_cpp//eval/eval:field_backed_list_impl",
        "@com_google_cel_cpp//eval/eval:field_backed_map_impl",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:value_export_util",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
        "@proxy_wasm_cpp_host//:lib",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
)
