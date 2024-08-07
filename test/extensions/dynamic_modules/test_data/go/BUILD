load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//test/extensions/dynamic_modules:__pkg__"])

go_binary(
    name = "no_op",
    srcs = ["no_op.go"],
    out = "libno_op.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/test/extensions/dynmic_modules/sdk/go",
    linkmode = "c-shared",
    deps = [
        "//source/extensions/dynamic_modules/sdk/go:sdk",
    ],
)
