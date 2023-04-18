load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "libgolang.so",
    srcs = [
        "main.go",
    ],
    out = "libgolang.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/go",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/utils",
        "//contrib/golang/filters/go/pkg/api",
        "//contrib/golang/filters/go/pkg/http",
        "//contrib/golang/filters/go/pkg/network",
        "@com_github_cncf_xds_go//udpa/type/v1:type",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
