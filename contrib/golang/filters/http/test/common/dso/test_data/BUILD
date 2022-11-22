load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "simple.so",
    srcs = [
        "simple.go",
    ],
    out = "simple.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/common/dso/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
)
