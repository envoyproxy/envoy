load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "plugin.so",
    srcs = [
        "config.go",
        "plugin.go",
    ],
    out = "plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/test/test_data/simple",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/router/cluster_specifier/source/go/pkg/api",
        "//contrib/golang/router/cluster_specifier/source/go/pkg/cluster_specifier",
        "@com_github_cncf_xds_go//xds/type/v3:type",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)
