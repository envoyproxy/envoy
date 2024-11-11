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
    importpath = "github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp:tcp_upstream",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)