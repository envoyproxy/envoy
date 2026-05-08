load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "ssl",
    srcs = [
        "filter.go",
    ],
    cgo = True,
    importpath = "example.com/test-data/ssl",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)
