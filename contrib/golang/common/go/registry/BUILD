load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "registry",
    srcs = ["registry.go"],
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/common/go/registry",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "//contrib/golang/http/cluster_specifier/source/go/pkg/cluster_specifier",
    ],
)
