load("@io_bazel_rules_go//go:def.bzl", "go_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = [
        "background_fetch_manager.cc",
        "dynamic_module_stats.cc",
        "dynamic_modules.cc",
    ],
    hdrs = [
        "background_fetch_manager.h",
        "dynamic_module_stats.h",
        "dynamic_modules.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/init:manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "abi_context_accessors_lib",
    srcs = ["abi_context_accessors.cc"],
    hdrs = ["abi_context_accessors.h"],
    deps = [
        "//envoy/common:optref_lib",
        "//envoy/formatter:http_formatter_context_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":dynamic_modules_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/synchronization",
    ],
    alwayslink = True,
)

go_library(
    name = "go_sdk_shared",
    srcs = [
        "sdk/go/shared/http.go",
        "sdk/go/shared/http_api.go",
        "sdk/go/shared/listener_api.go",
        "sdk/go/shared/listener_base.go",
        "sdk/go/shared/network_api.go",
        "sdk/go/shared/network_base.go",
        "sdk/go/shared/stats_sink.go",
        "sdk/go/shared/stats_sink_api.go",
        "sdk/go/shared/types.go",
    ],
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared",
    visibility = ["//visibility:public"],
)

go_library(
    name = "go_sdk",
    srcs = ["sdk/go/sdk.go"],
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go",
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/dynamic_modules:go_sdk_shared",
    ],
)

go_library(
    name = "go_sdk_internal_buffer",
    srcs = ["sdk/go/internal/buffer/buffer.go"],
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/internal/buffer",
    visibility = ["//visibility:public"],
)

go_library(
    name = "go_sdk_abi",
    srcs = [
        "sdk/go/abi/internal.go",
        "sdk/go/abi/listener.go",
        "sdk/go/abi/network.go",
        "//source/extensions/dynamic_modules/abi:abi.h",
    ],
    cgo = True,
    clinkopts = select({
        "//bazel:darwin_any": [
            "-Wl,-undefined,dynamic_lookup",
        ],
        "//conditions:default": [],
    }),
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi",
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/dynamic_modules:go_sdk",
        "//source/extensions/dynamic_modules:go_sdk_internal_buffer",
        "//source/extensions/dynamic_modules:go_sdk_shared",
    ],
)
