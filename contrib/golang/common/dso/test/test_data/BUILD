load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "simple.so",
    srcs = [
        "simple.go",
    ],
    out = "simple.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/common/dso/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
)

go_binary(
    name = "bad.so",
    srcs = [
        "bad.go",
    ],
    out = "bad.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/common/dso/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
)
