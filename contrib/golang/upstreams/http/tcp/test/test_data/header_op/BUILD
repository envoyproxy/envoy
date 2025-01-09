load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "bridge.so",
    srcs = [
        "bridge.go",
        "config.go",
    ],
    out = "bridge.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/test/test_data/header_op",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp:http_tcp_bridge",
        "@com_github_cncf_xds_go//xds/type/v3:type",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)
