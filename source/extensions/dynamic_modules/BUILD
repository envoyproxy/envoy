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
    srcs = ["dynamic_modules.cc"],
    hdrs = [
        "dynamic_modules.h",
    ],
    deps = [
        "//envoy/common:exception_lib",
        "//source/extensions/dynamic_modules/abi",
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
    ],
)

go_library(
    name = "go_sdk_shared",
    srcs = [
        "sdk/go/shared/api.go",
        "sdk/go/shared/base.go",
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
    name = "go_sdk_abi",
    srcs = [
        "sdk/go/abi/internal.go",
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
        "//source/extensions/dynamic_modules:go_sdk_shared",
    ],
)
