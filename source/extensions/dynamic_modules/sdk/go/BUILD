load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

exports_files(["abi.h"])  # Exported for tests.

go_library(
    name = "envoy_dynamic_modules_go_sdk",
    srcs = glob(["*.go"]) + ["abi.h"],
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go",
    visibility = ["//visibility:public"],
)
