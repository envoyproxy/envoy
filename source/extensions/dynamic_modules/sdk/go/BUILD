load("@io_bazel_rules_go//go:def.bzl", "go_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["abi.h"])

go_library(
    name = "sdk",
    srcs = [
        "abi.go",
        "abi.h",
        "envoy.go",
    ],
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go",
)
