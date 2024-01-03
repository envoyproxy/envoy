load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
    "envoy_pch_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "assert_lib",
    srcs = ["assert.cc"],
    hdrs = ["assert.h"],
    external_deps = [
        "abseil_base",
        "abseil_synchronization",
        "abseil_stacktrace",
        "abseil_symbolize",
    ],
    deps = [":minimal_logger_lib"],
)

envoy_cc_library(
    name = "containers_lib",
    hdrs = ["containers.h"],
    deps = [
        ":cleanup_lib",
    ],
)

envoy_cc_library(
    name = "debug_recursion_checker_lib",
    hdrs = ["debug_recursion_checker.h"],
    deps = [":assert_lib"],
)

envoy_cc_library(
    name = "backoff_lib",
    srcs = ["backoff_strategy.cc"],
    hdrs = ["backoff_strategy.h"],
    deps = [
        ":assert_lib",
        "//envoy/common:backoff_strategy_interface",
        "//envoy/common:random_generator_interface",
    ],
)

envoy_cc_library(
    name = "base64_lib",
    srcs = ["base64.cc"],
    hdrs = ["base64.h"],
    deps = [
        ":assert_lib",
        ":empty_string",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "byte_order_lib",
    hdrs = ["byte_order.h"],
)

envoy_cc_library(
    name = "c_smart_ptr_lib",
    hdrs = ["c_smart_ptr.h"],
)

envoy_cc_library(
    name = "cleanup_lib",
    hdrs = ["cleanup.h"],
    deps = [
        ":assert_lib",
    ],
)

envoy_cc_library(
    name = "compiler_requirements_lib",
    hdrs = ["compiler_requirements.h"],
)

envoy_cc_library(
    name = "dns_utils_lib",
    srcs = ["dns_utils.cc"],
    hdrs = ["dns_utils.h"],
    deps = [
        ":assert_lib",
        "//envoy/network:dns_interface",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "documentation_url_lib",
    hdrs = ["documentation_url.h"],
)

envoy_cc_library(
    name = "empty_string",
    hdrs = ["empty_string.h"],
)

envoy_cc_library(
    name = "enum_to_int",
    hdrs = ["enum_to_int.h"],
)

envoy_cc_library(
    name = "safe_memcpy_lib",
    hdrs = ["safe_memcpy.h"],
)

# fmt_lib is automatically a dependency of all envoy_cc_library definitions.
envoy_basic_cc_library(
    name = "fmt_lib",
    hdrs = ["fmt.h"],
    external_deps = [
        "abseil_strings",
        "fmtlib",
    ],
    deps = [
        "//envoy/common:base_includes",
        "@com_google_absl//absl/status",
    ],
)

envoy_cc_library(
    name = "hash_lib",
    srcs = ["hash.cc"],
    hdrs = ["hash.h"],
    external_deps = ["xxhash"],
    deps = [
        ":macros",
        ":safe_memcpy_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

envoy_cc_library(
    name = "hex_lib",
    srcs = ["hex.cc"],
    hdrs = ["hex.h"],
    deps = [":utility_lib"],
)

envoy_cc_library(
    name = "interval_value",
    hdrs = ["interval_value.h"],
)

envoy_cc_library(
    name = "key_value_store_lib",
    srcs = ["key_value_store_base.cc"],
    hdrs = ["key_value_store_base.h"],
    external_deps = [
        "abseil_cleanup",
        "quiche_quic_platform",
    ],
    deps = [
        "//envoy/common:key_value_store_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:filesystem_interface",
        "//source/common/config:ttl_lib",
        "@com_github_google_quiche//:quiche_common_lib",
    ],
)

envoy_cc_library(
    name = "linked_object",
    hdrs = ["linked_object.h"],
    deps = [":assert_lib"],
)

envoy_cc_library(
    name = "mem_block_builder_lib",
    hdrs = ["mem_block_builder.h"],
    deps = [":assert_lib"],
)

# Contains macros and helpers for dumpState utilities
envoy_cc_library(
    name = "dump_state_utils",
    hdrs = ["dump_state_utils.h"],
)

# Contains minimal code for logging to stderr.
envoy_cc_library(
    name = "minimal_logger_lib",
    srcs = [
        "fine_grain_logger.cc",
        "logger.cc",
    ],
    hdrs = [
        "fine_grain_logger.h",
        "json_escape_string.h",
        "logger.h",
    ],
    external_deps = [
        "abseil_synchronization",
        "abseil_flat_hash_map",
    ],
    deps = [
        ":base_logger_lib",
        ":fmt_lib",
        ":lock_guard_lib",
        ":macros",
        ":non_copyable",
        "//source/common/protobuf",
    ] + select({
        "//bazel:android_logger": ["logger_impl_lib_android"],
        "//conditions:default": ["logger_impl_lib_standard"],
    }),
)

envoy_cc_library(
    name = "base_logger_lib",
    srcs = ["base_logger.cc"],
    hdrs = ["base_logger.h"],
    external_deps = [
        "abseil_strings",
        "spdlog",
    ],
)

envoy_cc_library(
    name = "logger_impl_lib_standard",
    hdrs = ["standard/logger_impl.h"],
    include_prefix = "source/common/common",
    strip_include_prefix = "standard",
    deps = [":base_logger_lib"],
)

envoy_cc_library(
    name = "logger_impl_lib_android",
    srcs = select({
        "//bazel:android_logger": ["android/logger_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:android_logger": ["android/logger_impl.h"],
        "//conditions:default": [],
    }),
    include_prefix = "source/common/common",
    strip_include_prefix = "android",
    deps = [":base_logger_lib"],
)

envoy_cc_library(
    name = "win32_event_logger_impl_lib",
    srcs = select({
        "//bazel:windows_x86_64": ["win32/event_logger_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:windows_x86_64": ["win32/event_logger_impl.h"],
        "//conditions:default": [],
    }),
    deps = [":base_logger_lib"],
)

envoy_cc_library(
    name = "mutex_tracer_lib",
    srcs = ["mutex_tracer_impl.cc"],
    hdrs = ["mutex_tracer_impl.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":assert_lib",
        "//envoy/common:mutex_tracer",
    ],
)

# All non-essential logger delegates should go here to reduce dependencies that
# minimal_logger_lib maintains.
envoy_cc_library(
    name = "logger_lib",
    srcs = ["logger_delegates.cc"],
    hdrs = ["logger_delegates.h"],
    deps = [
        ":dump_state_utils",
        ":macros",
        ":minimal_logger_lib",
        "//envoy/access_log:access_log_interface",
    ],
)

envoy_cc_library(
    name = "basic_resource_lib",
    hdrs = ["basic_resource_impl.h"],
    deps = [
        "//envoy/common:resource_interface",
        "//envoy/runtime:runtime_interface",
    ],
)

envoy_cc_library(
    name = "macros",
    hdrs = ["macros.h"],
)

envoy_cc_library(
    name = "matchers_lib",
    srcs = ["matchers.cc"],
    hdrs = ["matchers.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":utility_lib",
        "//envoy/common:matchers_interface",
        "//source/common/common:regex_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:path_utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "random_generator_lib",
    srcs = [
        "random_generator.cc",
    ],
    hdrs = [
        "random_generator.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":assert_lib",
        "//envoy/common:random_generator_interface",
    ],
)

envoy_cc_library(
    name = "regex_lib",
    srcs = ["regex.cc"],
    hdrs = ["regex.h"],
    deps = [
        ":assert_lib",
        "//envoy/common:regex_interface",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@com_googlesource_code_re2//:re2",
        "@envoy_api//envoy/extensions/regex_engines/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "non_copyable",
    hdrs = ["non_copyable.h"],
)

envoy_cc_library(
    name = "phantom",
    hdrs = ["phantom.h"],
)

envoy_cc_library(
    name = "scope_tracker",
    hdrs = ["scope_tracker.h"],
    deps = [
        "//envoy/common:scope_tracker_interface",
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "scope_tracked_object_stack",
    hdrs = ["scope_tracked_object_stack.h"],
    deps = [
        "//envoy/common:scope_tracker_interface",
    ],
)

envoy_cc_library(
    name = "stl_helpers",
    hdrs = ["stl_helpers.h"],
)

envoy_cc_library(
    name = "thread_annotations",
    hdrs = ["thread_annotations.h"],
    external_deps = ["abseil_base"],
)

envoy_cc_library(
    name = "thread_synchronizer_lib",
    srcs = ["thread_synchronizer.cc"],
    hdrs = ["thread_synchronizer.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":assert_lib",
    ],
)

envoy_cc_library(
    name = "thread_lib",
    srcs = ["thread.cc"],
    hdrs = ["thread.h"],
    external_deps = ["abseil_synchronization"],
    deps = envoy_cc_platform_dep("thread_impl_lib") + [
        ":macros",
        ":non_copyable",
    ],
)

envoy_cc_posix_library(
    name = "thread_impl_lib",
    srcs = ["posix/thread_impl.cc"],
    hdrs = ["posix/thread_impl.h"],
    include_prefix = "source/common/common",
    strip_include_prefix = "posix",
    deps = [
        ":assert_lib",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_win32_library(
    name = "thread_impl_lib",
    srcs = ["win32/thread_impl.cc"],
    hdrs = ["win32/thread_impl.h"],
    include_prefix = "source/common/common",
    strip_include_prefix = "win32",
    deps = [
        ":assert_lib",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "lock_guard_lib",
    hdrs = ["lock_guard.h"],
    deps = [
        ":thread_annotations",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["abseil_node_hash_map"],
    deps = [
        ":assert_lib",
        ":hash_lib",
        ":non_copyable",
        "//envoy/common:exception_lib",
        "//envoy/common:interval_set_interface",
        "//envoy/common:time_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "packed_struct_lib",
    hdrs = ["packed_struct.h"],
    deps = [
        ":assert_lib",
        ":utility_lib",
        "//source/common/protobuf:utility_lib_header",
    ],
)

envoy_cc_library(
    name = "callback_impl_lib",
    srcs = ["callback_impl.cc"],
    hdrs = ["callback_impl.h"],
    deps = [
        ":assert_lib",
        ":lock_guard_lib",
        ":thread_lib",
        "//envoy/common:callback",
        "//source/common/event:dispatcher_lib",
    ],
)

envoy_cc_library(
    name = "perf_annotation_lib",
    srcs = ["perf_annotation.cc"],
    hdrs = ["perf_annotation.h"],
    deps = [
        ":assert_lib",
        ":thread_annotations",
        ":thread_lib",
        ":utility_lib",
    ],
)

envoy_cc_library(
    name = "perf_tracing_lib",
    srcs = ["perf_tracing.cc"],
    hdrs = ["perf_tracing.h"],
    deps = select({
        "//bazel:enable_perf_tracing": [
            "@com_github_google_perfetto//:perfetto",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "scalar_to_byte_vector_lib",
    hdrs = ["scalar_to_byte_vector.h"],
)

envoy_cc_library(
    name = "bit_array_lib",
    hdrs = ["bit_array.h"],
    deps = [
        ":safe_memcpy_lib",
    ],
)

envoy_cc_library(
    name = "token_bucket_impl_lib",
    srcs = ["token_bucket_impl.cc"],
    hdrs = ["token_bucket_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/common:token_bucket_interface",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "shared_token_bucket_impl_lib",
    srcs = ["shared_token_bucket_impl.cc"],
    hdrs = ["shared_token_bucket_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//source/common/common:thread_lib",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "statusor_lib",
    hdrs = ["statusor.h"],
    deps = [
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_pch_library(
    name = "common_pch",
    external_deps = [
        "spdlog",
    ],
    includes = [
        "envoy/config/bootstrap/v3/bootstrap.pb.h",
        "envoy/config/cluster/v3/cluster.pb.h",
        "envoy/config/core/v3/base.pb.h",
        "envoy/config/core/v3/config_source.pb.h",
        "envoy/config/route/v3/route_components.pb.h",
        "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h",
        "envoy/service/discovery/v3/discovery.pb.h",
        "spdlog/sinks/android_sink.h",
        "spdlog/spdlog.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "inline_map",
    hdrs = [
        "inline_map.h",
    ],
    external_deps = [
        "abseil_node_hash_set",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
    ],
)
