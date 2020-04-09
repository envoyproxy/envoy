licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
    "envoy_windows_extensions",
)
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS")

envoy_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

envoy_cc_binary(
    name = "envoy-static",
    stamped = True,
    deps = ["envoy_main_entry_lib"],
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
        "//bazel:windows_x86_64": envoy_windows_extensions(),
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
    ],
)

envoy_cc_library(
    name = "envoy_main_common_lib",
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
        "//source/common/stats:symbol_table_creator_lib",
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
    external_deps = ["ares"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/event:libevent_lib",
        "//source/common/http/http2:nghttp2_lib",
        "//source/server:proto_descriptors_lib",
    ],
)

envoy_cc_library(
    name = "platform_impl_lib",
    deps = [":platform_header_lib"] +
           envoy_cc_platform_dep("platform_impl_lib"),
)

envoy_cc_library(
    name = "platform_header_lib",
    hdrs = ["platform_impl.h"],
    deps = [
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_posix_library(
    name = "platform_impl_lib",
    srcs = ["posix/platform_impl.cc"],
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
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
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
