load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "simple.so",
    srcs = [
        "filter.go",
    ],
    out = "simple.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/examples/golang-network/simple",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/network/source/go/pkg/network",
        "@com_github_cncf_xds_go//xds/type/v3:type",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
