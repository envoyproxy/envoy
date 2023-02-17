load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "libgolang.so",
    srcs = [
        "main.go",
    ],
    out = "libgolang.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/http/cluster_specifier/source/go/pkg/api",
        "//contrib/golang/http/cluster_specifier/source/go/pkg/http",
        "//contrib/golang/common/go/utils",
        "@com_github_cncf_xds_go//udpa/type/v1:type",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
