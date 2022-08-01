load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_linux_library",
    "envoy_cc_platform_specific_dep",
    "envoy_cc_posix_without_linux_library",
    "envoy_cc_win32_library",
    "envoy_package",
)
load("//source/extensions:all_extensions.bzl", "envoy_all_core_extensions", "envoy_all_extensions")
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")

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
    stamped = True,
    deps = [":envoy_main_entry_lib"],
)

envoy_cc_library(
    name = "envoy_common_lib",
    deps = [
        "//source/common/event:libevent_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server:drain_manager_lib",
        "//source/server:options_lib",
        "//source/server:server_lib",
        "//source/server:listener_hooks_lib",
    ] + select({
        "//bazel:windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
)

envoy_cc_library(
    name = "envoy_main_entry_lib",
    srcs = ["main.cc"],
    external_deps = [
        "abseil_symbolize",
    ],
    deps = [
        ":envoy_main_common_lib",
        ":platform_impl_lib",
        ":scm_impl_lib",
    ],
)

envoy_cc_library(
    name = "main_common_lib",
    srcs = ["main_common.cc"],
    hdrs = ["main_common.h"],
    deps = [
        ":envoy_common_lib",
        ":platform_impl_lib",
        ":process_wide_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server/config_validation:server_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "//source/common/signal:sigaction_lib",
            ":terminate_handler_lib",
        ],
    }),
)

envoy_cc_library(
    name = "envoy_main_common_lib",
    deps = [
        ":main_common_lib",
        "//source/common/version:version_linkstamp",
    ],
)

envoy_cc_library(
    name = "envoy_common_with_core_extensions_lib",
    deps = [
               "//source/common/event:libevent_lib",
               "//source/common/network:utility_lib",
               "//source/common/stats:stats_lib",
               "//source/common/stats:thread_local_store_lib",
               "//source/server:drain_manager_lib",
               "//source/server:options_lib",
               "//source/server:server_lib",
               "//source/server:listener_hooks_lib",
           ] + envoy_all_core_extensions() +
           # TODO(rojkov): drop io_uring dependency when it's fully integrated.
           select({
               "//bazel:linux": ["//source/common/io:io_uring_impl_lib"],
               "//conditions:default": [],
           }),
)

envoy_cc_library(
    name = "envoy_main_common_with_core_extensions_lib",
    srcs = ["main_common.cc"],
    hdrs = ["main_common.h"],
    deps = [
        ":envoy_common_with_core_extensions_lib",
        ":platform_impl_lib",
        ":process_wide_lib",
        "//envoy/server:platform_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server/config_validation:server_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "//source/common/signal:sigaction_lib",
            ":terminate_handler_lib",
        ],
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
            ":main_common_lib",
            "//source/common/buffer:buffer_lib",
            "//source/common/common:assert_lib",
            "//source/common/common:win32_event_logger_impl_lib",
            "//source/common/common:thread_lib",
            "//source/common/event:signal_lib",
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
