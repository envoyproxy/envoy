load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "buffered",
    srcs = [
        "bridge.go",
        "config.go",
    ],
    cgo = True,
    importpath = "example.com/test-data/buffered",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp:http_tcp_bridge",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
        "@xds_go//xds/type/v3:type",
    ],
)
