load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "api",
    srcs = [
        "capi.go",
        "cluster.go",
    ],
    importpath = "github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/source/go/pkg/api",
    visibility = ["//visibility:public"],
    deps = [
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
