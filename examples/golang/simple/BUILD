load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "filter.so",
    srcs = [
        "config.go",
        "filter.go",
    ],
    out = "filter.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/examples/golang/simple",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)
