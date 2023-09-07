load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "wasm_runtime_factory_interface",
    hdrs = [
        "wasm_runtime_factory.h",
    ],
    deps = [
        "//envoy/config:typed_config_interface",
        "@proxy_wasm_cpp_host//:wasm_vm_headers",
    ],
    alwayslink = 1,
)

# NB: Used to break the circular dependency between wasm_lib and null_plugin_lib.
envoy_cc_library(
    name = "wasm_hdr",
    hdrs = [
        "context.h",
        "plugin.h",
        "stats_handler.h",
        "wasm.h",
        "wasm_vm.h",
    ],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//envoy/stats:custom_stat_namespaces_interface",
        "//envoy/thread_local:thread_local_object",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stats:stats_lib",
        "//source/common/version:version_includes",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
        "@proxy_wasm_cpp_host//:headers",
        "@proxy_wasm_cpp_sdk//:common_lib",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "wasm_lib",
    srcs = [
        "context.cc",
        "foreign.cc",
        "plugin.cc",
        "stats_handler.cc",
        "wasm.cc",
        "wasm_vm.cc",
    ],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DWASM_USE_CEL_PARSER",
        ],
    }),
    visibility = [
        "//source/extensions:__subpackages__",
        "//test/extensions:__subpackages__",
        "//test/test_common:__subpackages__",
    ],
    deps = [
        ":wasm_hdr",
        ":wasm_runtime_factory_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//external:abseil_base",
        "//external:abseil_node_hash_map",
        "//external:zlib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/common/wasm/ext:declare_property_cc_proto",
        "//source/extensions/common/wasm/ext:envoy_null_vm_wasm_api",
        "//source/extensions/common/wasm/ext:set_envoy_filter_state_cc_proto",
        "//source/extensions/filters/common/expr:context_lib",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:value_export_util",
        "@com_google_cel_cpp//eval/public/containers:field_access",
        "@com_google_cel_cpp//eval/public/containers:field_backed_list_impl",
        "@com_google_cel_cpp//eval/public/containers:field_backed_map_impl",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
        "@proxy_wasm_cpp_host//:base_lib",
        "@proxy_wasm_cpp_host//:null_lib",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
    alwayslink = 1,
)
