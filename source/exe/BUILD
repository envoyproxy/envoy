load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_linux_library",
    "envoy_cc_platform_specific_dep",
    "envoy_cc_posix_without_linux_library",
    "envoy_cc_win32_library",
    "envoy_package",
    "envoy_select_admin_functionality",
    "envoy_select_enable_http3",
    "envoy_select_signal_trace",
)
load("//bazel:repositories.bzl", "NO_HTTP3_SKIP_TARGETS", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")
load("//source/extensions:all_extensions.bzl", "envoy_all_core_extensions", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

envoy_cc_binary(
    name = "envoy-static",
    features = select({
        "//bazel:windows_opt_build": ["generate_pdb_file"],
        "//conditions:default": [],
    }),
    rbe_pool = "2core",
    stamped = True,
    deps = [":envoy_main_entry_lib"],
)

envoy_cc_library(
    name = "all_extensions_lib",
    deps = [
        "//source/common/event:libevent_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server:drain_manager_lib",
        "//source/server:listener_hooks_lib",
        "//source/server:options_base",
        "//source/server:server_base_lib",
    ] + select({
        "//bazel:enable_http3_on_windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:enable_http3_on_linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//bazel:disable_http3_on_windows_x86_64": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS + WINDOWS_SKIP_TARGETS),
        "//bazel:disable_http3_on_linux_ppc": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS + PPC_SKIP_TARGETS),
        "//bazel:disable_http3": envoy_all_extensions(NO_HTTP3_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
)

envoy_cc_library(
    name = "envoy_main_entry_lib",
    srcs = ["main.cc"],
    rbe_pool = "2core",
    deps = [
        ":envoy_main_common_lib",
        ":platform_impl_lib",
        ":scm_impl_lib",
        "//source/server:options_lib",
        "@com_google_absl//absl/debugging:symbolize",
    ],
)

envoy_cc_library(
    name = "stripped_main_base_lib",
    srcs = ["stripped_main_base.cc"],
    hdrs = ["stripped_main_base.h"],
    rbe_pool = "2core",
    deps = [
        ":platform_impl_lib",
        ":process_wide_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/event:libevent_lib",
        "//source/common/event:real_time_system_lib",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/server:drain_manager_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:listener_hooks_lib",
        "//source/server:options_base",
        "//source/server:server_base_lib",
    ] + envoy_select_signal_trace([
        "//source/common/signal:sigaction_lib",
        ":terminate_handler_lib",
    ]),
)

envoy_cc_library(
    name = "main_common_lib",
    srcs = [
        "main_common.cc",
    ],
    hdrs = [
        "main_common.h",
    ],
    rbe_pool = "2core",
    deps = envoy_select_admin_functionality([":admin_response_lib"]) + [
        ":platform_impl_lib",
        ":process_wide_lib",
        ":stripped_main_base_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:options_lib",
        "//source/server/config_validation:server_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "admin_response_lib",
    srcs = ["admin_response.cc"],
    hdrs = ["admin_response.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/server:server_lib",
        "//source/server/admin:admin_lib",
        "//source/server/admin:utils_lib",
    ],
)

envoy_cc_library(
    name = "main_common_with_all_extensions_lib",
    deps = [
        ":all_extensions_lib",
        ":main_common_lib",
        ":platform_impl_lib",
        ":process_wide_lib",
        ":stripped_main_base_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:options_lib",
        "//source/server/config_validation:server_lib",
    ],
)

# provides a library target for Envoy server builds with the versioning information set up correctly.
envoy_cc_library(
    name = "envoy_main_common_lib",
    deps = [
        ":main_common_with_all_extensions_lib",
        # These are compiled as extensions so Envoy Mobile doesn't have to link them in.
        # Envoy requires them.
        "//source/common/tls:server_context_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/common/listener_manager:listener_manager_lib",
        "//source/extensions/listener_managers/validation_listener_manager:validation_listener_manager_lib",
        "//source/common/version:version_linkstamp",
        "//source/common/formatter:formatter_extension_lib",
        "//source/server:options_lib",
        # TODO(wbpcode) make this a proper extension
        "//source/extensions/filters/http/match_delegate:config",
        "//source/common/http:rds_lib",
        "//source/common/router:rds_lib",
        "//source/common/router:scoped_rds_lib",
    ] + envoy_select_enable_http3(["//source/common/quic:server_codec_lib"]),
)

# provides a library target for Envoy Mobile builds with the versioning information set up correctly.
envoy_cc_library(
    name = "envoy_stripped_main_base_lib",
    deps = [
        ":stripped_main_base_lib",
        "//source/common/version:version_linkstamp",
    ],
)

envoy_cc_library(
    name = "envoy_main_common_with_core_extensions_lib",
    deps = [
               ":main_common_lib",
               ":platform_impl_lib",
               ":process_wide_lib",
               ":stripped_main_base_lib",
               "//envoy/server:platform_interface",
               "//source/common/api:os_sys_calls_lib",
               "//source/common/common:compiler_requirements_lib",
               "//source/common/common:perf_annotation_lib",
               "//source/common/grpc:google_grpc_context_lib",
               "//source/extensions/listener_managers/validation_listener_manager:validation_listener_manager_lib",
               "//source/server:hot_restart_lib",
               "//source/server:hot_restart_nop_lib",
               "//source/server:options_lib",
               "//source/server:server_lib",
               "//source/server/config_validation:server_lib",
           ] + envoy_all_core_extensions() +
           # TODO(rojkov): drop io_uring dependency when it's fully integrated.
           select({
               "//bazel:linux": ["//source/common/io:io_uring_impl_lib"],
               "//conditions:default": [],
           }),
)

envoy_cc_library(
    name = "process_wide_lib",
    srcs = ["process_wide.cc"],
    hdrs = ["process_wide.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/event:libevent_lib",
        "//source/common/http/http2:nghttp2_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/server:proto_descriptors_lib",
    ],
)

envoy_cc_library(
    name = "platform_impl_lib",
    deps = [":platform_header_lib"] +
           envoy_cc_platform_specific_dep("platform_impl_lib"),
)

envoy_cc_library(
    name = "platform_header_lib",
    hdrs = ["platform_impl.h"],
    deps = [
        "//envoy/server:platform_interface",
    ],
)

envoy_cc_posix_without_linux_library(
    name = "platform_impl_lib",
    srcs = ["posix/platform_impl.cc"],
    deps = [
        ":platform_header_lib",
        "//source/common/common:thread_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_linux_library(
    name = "platform_impl_lib",
    srcs = ["linux/platform_impl.cc"],
    deps = [
        ":platform_header_lib",
        "//source/common/common:thread_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_win32_library(
    name = "platform_impl_lib",
    srcs = ["win32/platform_impl.cc"],
    deps = [
        ":platform_header_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:signal_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_library(
    name = "scm_impl_lib",
    srcs = select({
        "//bazel:windows_x86_64": [
            "win32/service_base.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:windows_x86_64": [
            "win32/service_base.h",
            "win32/service_status.h",
        ],
        "//conditions:default": [],
    }),
    include_prefix = "source/exe",
    strip_include_prefix = select({
        "//bazel:windows_x86_64": "win32",
        "//conditions:default": "",
    }),
    deps = select({
        "//bazel:windows_x86_64": [
            ":main_common_with_all_extensions_lib",
            "//source/common/buffer:buffer_lib",
            "//source/common/common:assert_lib",
            "//source/common/common:thread_lib",
            "//source/common/common:win32_event_logger_impl_lib",
            "//source/common/event:signal_lib",
            "//source/server:options_lib",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "terminate_handler_lib",
    srcs = ["terminate_handler.cc"],
    hdrs = ["terminate_handler.h"],
    tags = ["backtrace"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:non_copyable",
        "//source/server:backtrace_lib",
    ],
)
